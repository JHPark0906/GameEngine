#include "pch.h"
#include "D3D12BufferUpload.h"

#include <cstring>

#include "D3D12ResourceDescriptions.h"
#include "../../Platform/Win32/Win32Diagnostics.h"

namespace GameEngine::Rendering::D3D12
{

using Platform::Win32::LogHResult;

bool PrepareBufferUpload(
    ID3D12Device& device,
    const void* const source,
    const std::size_t byteSize,
    Microsoft::WRL::ComPtr<ID3D12Resource>& buffer,
    Microsoft::WRL::ComPtr<ID3D12Resource>& upload)
{
    const D3D12_RESOURCE_DESC description = BufferDescription(static_cast<UINT64>(byteSize));

    const D3D12_HEAP_PROPERTIES defaultHeapProperties = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT result = device.CreateCommittedResource(
        &defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(buffer.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating a D3D12 buffer", result);
        return false;
    }

    const D3D12_HEAP_PROPERTIES uploadHeapProperties = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    result = device.CreateCommittedResource(
        &uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(upload.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating a D3D12 staging buffer", result);
        return false;
    }

    std::byte* mapped = nullptr;
    result = upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    if (FAILED(result))
    {
        LogHResult("Mapping a D3D12 staging buffer", result);
        return false;
    }
    std::memcpy(mapped, source, byteSize);
    upload->Unmap(0, nullptr);
    return true;
}

void RecordBufferUpload(
    ID3D12GraphicsCommandList& commandList,
    ID3D12Resource& buffer, ID3D12Resource& upload,
    const std::size_t byteSize, const D3D12_RESOURCE_STATES finalState)
{
    commandList.CopyBufferRegion(&buffer, 0, &upload, 0, byteSize);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = &buffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = finalState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList.ResourceBarrier(1, &barrier);
}

}
