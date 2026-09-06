#include "D3D12MeshUploadLifetimeTests.h"

#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>

#include "Assets/MeshData.h"
#include "Assets/SkinnedMeshData.h"
#include "Rendering/D3D12/D3D12GraphicsDevice.h"
#include "Rendering/D3D12/D3D12MeshUploader.h"
#include "Rendering/D3D12/D3D12ResourceDescriptions.h"
#include "Rendering/D3D12/D3D12SkinnedMeshUploader.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using namespace Rendering::D3D12;
    using Microsoft::WRL::ComPtr;
    using TestSupport::Expect;

    struct Submission
    {
        ComPtr<ID3D12CommandQueue> queue;
        std::array<ComPtr<ID3D12CommandAllocator>, 2> allocators;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Fence> completion;
        ComPtr<ID3D12Fence> gate;
        HANDLE event = nullptr;
        UINT64 fenceValue = 0;

        ~Submission()
        {
            // Assertions may fail with work queued behind the gate. Release it and drain before
            // the caller destroys its uploader, even when the regression is present.
            if (gate) static_cast<void>(gate->Signal(1));
            if (queue && completion && event && SUCCEEDED(queue->Signal(completion.Get(), ++fenceValue)))
                static_cast<void>(Wait());
            if (event) CloseHandle(event);
        }

        bool Initialize(ID3D12Device& device)
        {
            D3D12_COMMAND_QUEUE_DESC description{};
            if (FAILED(device.CreateCommandQueue(&description, IID_PPV_ARGS(queue.GetAddressOf())))) return false;
            for (auto& allocator : allocators)
                if (FAILED(device.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                        IID_PPV_ARGS(allocator.GetAddressOf())))) return false;
            if (FAILED(device.CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[0].Get(),
                    nullptr, IID_PPV_ARGS(list.GetAddressOf()))) ||
                FAILED(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(completion.GetAddressOf()))) ||
                FAILED(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(gate.GetAddressOf())))) return false;
            event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            return event != nullptr;
        }

        bool Execute()
        {
            if (FAILED(list->Close())) return false;
            ID3D12CommandList* commands[]{ list.Get() };
            queue->ExecuteCommandLists(1, commands);
            return SUCCEEDED(queue->Signal(completion.Get(), ++fenceValue));
        }

        bool Wait() const
        {
            return SUCCEEDED(completion->SetEventOnCompletion(fenceValue, event)) &&
                WaitForSingleObject(event, 5000) == WAIT_OBJECT_0;
        }
    };

    bool CopyForReadback(ID3D12Device& device, ID3D12GraphicsCommandList& list,
        ID3D12Resource& source, const D3D12_RESOURCE_STATES state, const std::size_t bytes,
        ComPtr<ID3D12Resource>& readback)
    {
        const auto heap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
        const auto description = BufferDescription(bytes);
        if (FAILED(device.CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf())))) return false;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = &source;
        barrier.Transition.StateBefore = state;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list.ResourceBarrier(1, &barrier);
        list.CopyBufferRegion(readback.Get(), 0, &source, 0, bytes);
        return true;
    }

    bool Matches(ID3D12Resource& readback, const void* expected, const std::size_t bytes)
    {
        void* mapped = nullptr;
        const D3D12_RANGE readRange{ 0, bytes };
        if (FAILED(readback.Map(0, &readRange, &mapped))) return false;
        const bool equal = std::memcmp(mapped, expected, bytes) == 0;
        const D3D12_RANGE noWrites{ 0, 0 };
        readback.Unmap(0, &noWrites);
        return equal;
    }

    template<class TMesh, class TUploader>
    bool CheckLifetime(ID3D12Device& device, const char* label)
    {
        // Submission is destroyed first and drains the queue before GPU resource owners go away.
        TUploader uploader;
        ComPtr<ID3D12Resource> vertexReadback, indexReadback;
        Submission submission;
        if (!Expect(submission.Initialize(device), "the mesh retirement submission should initialize")) return false;
        uploader.Initialize(device);
        TMesh mesh;
        mesh.id = 0x1000'0000'0000'0A21ull;
        mesh.vertices.resize(3);
        mesh.vertices[0].position = { 1.25f, 2.5f, 3.75f };
        mesh.vertices[1].position = { 4.5f, 5.75f, 6.25f };
        mesh.vertices[2].position = { -7.5f, -8.25f, -9.75f };
        mesh.indices = { 2, 0, 1 };
        uploader.BeginFrame(2);
        const auto* initial = uploader.Resolve(*submission.list.Get(), mesh);
        if (!Expect(initial && uploader.GetPendingUploadCount() == 2,
                "a mesh submission must own both staging resources")) return false;
        const auto oldGeometry = initial->vertexBuffer;
        if (!Expect(submission.Execute() && submission.Wait(), "the original capture slot must finish")) return false;
        // Slot 2 stays inactive while ordinary frames advance and evict its unused mesh binding.
        for (unsigned int frame = 0; frame < 310; ++frame) uploader.BeginFrame(frame % 2);
        bool passed = Expect(uploader.GetPendingUploadCount() == 0,
            "completion of later slots should retire staging even when its original slot stays inactive");
        if (!Expect(SUCCEEDED(submission.list->Reset(submission.allocators[1].Get(), nullptr)),
                "a separate allocator should record the new mesh upload")) return false;
        uploader.BeginFrame(0);
        const auto* fresh = uploader.Resolve(*submission.list.Get(), mesh);
        if (!Expect(fresh && fresh->vertexBuffer.Get() != oldGeometry.Get() && uploader.GetPendingUploadCount() == 2,
                "an evicted mesh must create a new upload under the same mesh ID")) return false;
        const std::size_t vertexBytes = mesh.vertices.size() * sizeof(mesh.vertices[0]);
        const std::size_t indexBytes = mesh.indices.size() * sizeof(mesh.indices[0]);
        if (!Expect(CopyForReadback(device, *submission.list.Get(), *fresh->vertexBuffer.Get(),
                    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, vertexBytes, vertexReadback) &&
                CopyForReadback(device, *submission.list.Get(), *fresh->indexBuffer.Get(),
                    D3D12_RESOURCE_STATE_INDEX_BUFFER, indexBytes, indexReadback),
                "the newly uploaded mesh should be copied for byte validation")) return false;
        if (!Expect(SUCCEEDED(submission.queue->Wait(submission.gate.Get(), 1)) && submission.Execute(),
                "the new submission should wait behind a CPU-controlled gate")) return false;
        uploader.BeginFrame(2, false);
        passed &= Expect(submission.completion->GetCompletedValue() < submission.fenceValue &&
                uploader.GetPendingUploadCount() == 2,
            "returning to an old capture slot must retain another slot's unfinished upload");
        if (!Expect(SUCCEEDED(submission.gate->Signal(1)) && submission.Wait(),
                "the mesh upload should complete after the gate opens")) return false;
        passed &= Expect(Matches(*vertexReadback.Get(), mesh.vertices.data(), vertexBytes) &&
                Matches(*indexReadback.Get(), mesh.indices.data(), indexBytes),
            "GPU vertex and index bytes must survive eviction, reupload and old-slot reactivation");
        uploader.BeginFrame(0);
        passed &= Expect(uploader.GetPendingUploadCount() == 0,
            "the new submission's staging must retire after its own completion is known");
        if (!passed) std::cerr << "Mesh upload lifetime regression: " << label << '\n';
        return passed;
    }
}

bool RunD3D12MeshUploadLifetimeTests()
{
    if (!D3D12GraphicsDevice::IsHardwareSupported())
    {
        std::cout << "  mesh upload lifetime tests skipped: D3D12 hardware unavailable\n";
        return true;
    }
    ComPtr<ID3D12Device> device;
    if (!Expect(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(device.GetAddressOf()))), "the D3D12 mesh lifetime device should initialize")) return false;
    bool passed = CheckLifetime<Assets::MeshData, D3D12MeshUploader>(*device.Get(), "static");
    passed &= CheckLifetime<Assets::SkinnedMeshData, D3D12SkinnedMeshUploader>(*device.Get(), "skinned");
    return passed;
}

static const TestSupport::Registration gD3D12MeshUploadLifetimeTests{
    "BackendImage", "D3D12 mesh uploads should survive cache eviction and capture slot reuse", RunD3D12MeshUploadLifetimeTests };
