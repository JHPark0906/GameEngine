#include "pch.h"
#include "D3D12SkinnedMeshUploader.h"

#include "../ShaderInterop.h"

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <utility>

#include "D3D12BufferUpload.h"
#include "D3D12FrameResources.h"
#include "D3D12UploadBufferPool.h"
#include "../FrameBoundCache.h"
#include "../../Assets/SkinnedMeshData.h"
#include "../../Diagnostics/Debug.h"
#include "../RenderResourceCachePolicy.h"

namespace GameEngine::Rendering::D3D12
{

struct D3D12SkinnedMeshUploader::Implementation
{
    ID3D12Device* device = nullptr;

    // D3D12MeshUploader와 같은 이유로 in flight 프레임 수만큼 보존한다.
    FrameBoundCache<std::uint64_t, D3D12SkinnedMeshBinding> bindings{
        MeshCacheBudget.maximumEntries, MeshCacheBudget.maximumBytes, FrameSlotRetention };
    // 업로드 자원은 메시 캐시가 아니라 제출이 소유한다. 완료된 같은 큐의 앞선 제출도
    // 함께 회수하므로 비활성 캡처 슬롯이 스테이징을 계속 붙잡지 않는다.
    D3D12UploadBufferPool<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameSlotCount>
        uploads{ D3D12UploadPoolBudget{ 0, 0 } };
    std::unordered_set<std::uint64_t> failedMeshes;
    std::unordered_set<std::uint64_t> capacityRejectedMeshes;
    CapacityWarningThrottle capacityWarning;
};

D3D12SkinnedMeshUploader::D3D12SkinnedMeshUploader()
    : mImplementation(std::make_unique<Implementation>())
{
}

D3D12SkinnedMeshUploader::~D3D12SkinnedMeshUploader() = default;

void D3D12SkinnedMeshUploader::Initialize(ID3D12Device& device)
{
    mImplementation->device = &device;
}

void D3D12SkinnedMeshUploader::BeginFrame(const UINT frameIndex, const bool advanceCache)
{
    Implementation& data = *mImplementation;
    data.uploads.BeginFrame(frameIndex, advanceCache);
    if (advanceCache)
    {
        data.bindings.BeginFrame();
    }
    data.capacityRejectedMeshes.clear();
    data.capacityWarning.BeginFrame();
}

std::size_t D3D12SkinnedMeshUploader::GetPendingUploadCount() const
{
    return mImplementation->uploads.GetInFlightCount();
}

const D3D12SkinnedMeshBinding* D3D12SkinnedMeshUploader::Resolve(
    ID3D12GraphicsCommandList& commandList, const Assets::SkinnedMeshData& mesh)
{
    Implementation& data = *mImplementation;
    if (!data.device || !mesh.IsValid())
    {
        return nullptr;
    }
    if (const D3D12SkinnedMeshBinding* const cached = data.bindings.Find(mesh.id))
    {
        return cached;
    }
    if (data.failedMeshes.contains(mesh.id) || data.capacityRejectedMeshes.contains(mesh.id))
    {
        return nullptr;
    }

    const std::size_t byteSize = mesh.GetByteSize();
    if (!data.bindings.MakeRoom(byteSize))
    {
        data.capacityRejectedMeshes.insert(mesh.id);
        data.capacityWarning.Warn(
            "D3D12 skinned mesh binding cache reached its entry or memory limit.");
        return nullptr;
    }

    const std::size_t vertexBytes = mesh.vertices.size() * sizeof(SkinnedMeshVertex);
    const std::size_t indexBytes = mesh.indices.size() * sizeof(std::uint32_t);

    D3D12SkinnedMeshBinding binding;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexUpload;
    if (!PrepareBufferUpload(
            *data.device, mesh.vertices.data(), vertexBytes,
            binding.vertexBuffer, vertexUpload) ||
        !PrepareBufferUpload(
            *data.device, mesh.indices.data(), indexBytes,
            binding.indexBuffer, indexUpload))
    {
        data.failedMeshes.insert(mesh.id);
        return nullptr;
    }

    binding.vertexBufferView = {
        binding.vertexBuffer->GetGPUVirtualAddress(),
        static_cast<UINT>(vertexBytes),
        sizeof(SkinnedMeshVertex)
    };
    binding.indexBufferView = {
        binding.indexBuffer->GetGPUVirtualAddress(),
        static_cast<UINT>(indexBytes),
        DXGI_FORMAT_R32_UINT
    };
    binding.indexCount = static_cast<UINT>(mesh.indices.size());

    if (!data.uploads.Commit({ vertexUpload, vertexBytes }) ||
        !data.uploads.Commit({ indexUpload, indexBytes }))
    {
        Diagnostics::Debug::LogError("The D3D12 skinned mesh uploader has no active frame slot.");
        return nullptr;
    }
    const D3D12SkinnedMeshBinding* const cached =
        data.bindings.Insert(mesh.id, std::move(binding), byteSize);
    if (!cached)
    {
        Diagnostics::Debug::LogError("D3D12 skinned mesh binding cache rejected a duplicate key.");
        return nullptr;
    }
    RecordBufferUpload(commandList, *cached->vertexBuffer.Get(), *vertexUpload.Get(),
        vertexBytes, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    RecordBufferUpload(commandList, *cached->indexBuffer.Get(), *indexUpload.Get(),
        indexBytes, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    return cached;
}

}
