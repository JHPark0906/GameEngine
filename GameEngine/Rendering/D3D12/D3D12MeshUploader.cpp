#include "pch.h"
#include "D3D12MeshUploader.h"

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
#include "../../Assets/MeshData.h"
#include "../../Diagnostics/Debug.h"
#include "../RenderResourceCachePolicy.h"

namespace GameEngine::Rendering::D3D12
{

struct D3D12MeshUploader::Implementation
{
    ID3D12Device* device = nullptr;

    // Retained for as many frames as run in flight, so a binding an executing command list draws
    // from is never released.
    FrameBoundCache<std::uint64_t, D3D12MeshBinding> bindings{
        MeshCacheBudget.maximumEntries, MeshCacheBudget.maximumBytes, FrameSlotRetention };
    // 캐시의 메시 ID와 업로드 수명은 독립이다. 재사용 캐시는 두지 않고, 제출 완료까지
    // 실제 자원을 소유한다. 한 슬롯의 완료는 같은 큐의 앞선 비활성 슬롯도 함께 회수한다.
    D3D12UploadBufferPool<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameSlotCount>
        uploads{ D3D12UploadPoolBudget{ 0, 0 } };
    std::unordered_set<std::uint64_t> failedMeshes;
    std::unordered_set<std::uint64_t> capacityRejectedMeshes;
    CapacityWarningThrottle capacityWarning;
};

D3D12MeshUploader::D3D12MeshUploader()
    : mImplementation(std::make_unique<Implementation>())
{
}

D3D12MeshUploader::~D3D12MeshUploader() = default;

void D3D12MeshUploader::Initialize(ID3D12Device& device)
{
    mImplementation->device = &device;
}

void D3D12MeshUploader::BeginFrame(const UINT frameIndex, const bool advanceCache)
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

std::size_t D3D12MeshUploader::GetPendingUploadCount() const
{
    return mImplementation->uploads.GetInFlightCount();
}

const D3D12MeshBinding* D3D12MeshUploader::Resolve(
    ID3D12GraphicsCommandList& commandList, const Assets::MeshData& mesh)
{
    Implementation& data = *mImplementation;
    if (!data.device || !mesh.IsValid())
    {
        return nullptr;
    }
    if (const D3D12MeshBinding* const cached = data.bindings.Find(mesh.id))
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
        data.capacityWarning.Warn("D3D12 mesh binding cache reached its entry or memory limit.");
        return nullptr;
    }

    const std::size_t vertexBytes = mesh.vertices.size() * sizeof(MeshVertex);
    const std::size_t indexBytes = mesh.indices.size() * sizeof(std::uint32_t);

    D3D12MeshBinding binding;
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
        sizeof(MeshVertex)
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
        Diagnostics::Debug::LogError("The D3D12 mesh uploader has no active frame slot.");
        return nullptr;
    }
    const D3D12MeshBinding* const cached =
        data.bindings.Insert(mesh.id, std::move(binding), byteSize);
    if (!cached)
    {
        Diagnostics::Debug::LogError("D3D12 mesh binding cache rejected a duplicate key.");
        return nullptr;
    }
    RecordBufferUpload(commandList, *cached->vertexBuffer.Get(), *vertexUpload.Get(),
        vertexBytes, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    RecordBufferUpload(commandList, *cached->indexBuffer.Get(), *indexUpload.Get(),
        indexBytes, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    // 업로드마다 로그하지 않는다: 동적 텍스처와 텍스트는 프레임마다 업로드될 수 있고, 그 로그가
    // 에디터 콘솔에 보이면 콘솔을 그리는 일이 새 로그를 낳는 되먹임이 된다.
    return cached;
}

}
