#include "pch.h"
#include "D3D12TextureUploader.h"


#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_set>
#include <utility>
#include <wrl/client.h>

#include "D3D12ResolvedResources.h"
#include "D3D12ResourceDescriptions.h"
#include "D3D12UploadBufferPool.h"
#include "../../Diagnostics/Debug.h"
#include "../../Platform/Win32/Win32Diagnostics.h"
#include "../FrameBoundCache.h"
#include "../RenderResourceCachePolicy.h"

namespace GameEngine::Rendering::D3D12
{

using Platform::Win32::LogHResult;

namespace
{
    using UploadPool = D3D12UploadBufferPool<
        Microsoft::WRL::ComPtr<ID3D12Resource>, FrameSlotCount>;

    /// <summary>
    /// 픽셀을 업로드 힙에 복사하고 텍스처를 PIXEL_SHADER_RESOURCE 상태로 만든다. 풀은 첫 GPU
    /// 명령을 기록하기 전에 업로드 버퍼를 소유하고 해당 제출의 완료까지 수명을 유지한다.
    /// </summary>
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource> RecordPixelUpload(
        ID3D12Device& device,
        ID3D12GraphicsCommandList& commandList,
        UploadPool& uploadPool,
        const D3D12ResolvedTexture& sourceTexture,
        ID3D12Resource& texture,
        const D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COPY_DEST)
    {
        const D3D12_RESOURCE_DESC textureDescription = texture.GetDesc();
        UINT64 uploadSize = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT rows = 0;
        UINT64 rowSize = 0;
        device.GetCopyableFootprints(
            &textureDescription, 0, 1, 0, &footprint, &rows, &rowSize, &uploadSize);
        constexpr UINT64 alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        if (uploadSize == 0 ||
            uploadSize > (std::numeric_limits<std::size_t>::max)() - (alignment - 1))
        {
            Diagnostics::Debug::LogError("The D3D12 texture upload footprint is too large.");
            return nullptr;
        }
        const std::size_t capacity = static_cast<std::size_t>(
            (uploadSize + alignment - 1) / alignment * alignment);
        UploadPool::Buffer buffer = uploadPool.Take(capacity);
        if (!buffer.resource)
        {
            const D3D12_HEAP_PROPERTIES uploadHeapProperties = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
            const D3D12_RESOURCE_DESC uploadDescription = BufferDescription(capacity);
            const HRESULT result = device.CreateCommittedResource(
                &uploadHeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &uploadDescription,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(buffer.resource.ReleaseAndGetAddressOf()));
            if (FAILED(result))
            {
                LogHResult("Creating the D3D12 texture upload buffer", result);
                return nullptr;
            }
            buffer.capacity = capacity;
        }

        std::byte* destinationPixels = nullptr;
        const D3D12_RANGE noCpuReads{ 0, 0 };
        const HRESULT result = buffer.resource->Map(
            0, &noCpuReads, reinterpret_cast<void**>(&destinationPixels));
        if (FAILED(result))
        {
            LogHResult("Mapping the D3D12 texture upload buffer", result);
            return nullptr;
        }
        const std::size_t sourcePitch =
            static_cast<std::size_t>(sourceTexture.width) * sourceTexture.bytesPerPixel;
        for (UINT row = 0; row < rows; ++row)
        {
            std::memcpy(
                destinationPixels + footprint.Offset +
                    static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                sourceTexture.pixels->data() + static_cast<std::size_t>(row) * sourcePitch,
                sourcePitch);
        }
        buffer.resource->Unmap(0, nullptr);
        if (!uploadPool.Commit(buffer))
        {
            Diagnostics::Debug::LogError("The D3D12 texture uploader has no active frame slot.");
            return nullptr;
        }

        // Allocation and mapping are complete before recording the first state change. A failed
        // update leaves the old texture drawable and may be retried on the next frame.
        if (initialState != D3D12_RESOURCE_STATE_COPY_DEST)
        {
            D3D12_RESOURCE_BARRIER transition = {};
            transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            transition.Transition.pResource = &texture;
            transition.Transition.StateBefore = initialState;
            transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList.ResourceBarrier(1, &transition);
        }

        D3D12_TEXTURE_COPY_LOCATION destination = {
            &texture,
            D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX
        };
        D3D12_TEXTURE_COPY_LOCATION source = {
            buffer.resource.Get(),
            D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT
        };
        source.PlacedFootprint = footprint;
        commandList.CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = &texture;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList.ResourceBarrier(1, &barrier);
        return buffer.resource;
    }

    [[nodiscard]] bool UploadTexture(
        ID3D12Device& device,
        D3D12DescriptorHeapAllocator& descriptorAllocator,
        ID3D12GraphicsCommandList& commandList,
        UploadPool& uploadPool,
        const D3D12ResolvedTexture& sourceTexture,
        D3D12TextureBinding& destinationTexture,
        Microsoft::WRL::ComPtr<ID3D12Resource>& upload)
    {
        if (!sourceTexture.IsValid())
        {
            Diagnostics::Debug::LogError("D3D12 texture upload source is invalid.");
            return false;
        }
        // Own the slot immediately so every failure path below returns it to the allocator.
        destinationTexture.descriptor = descriptorAllocator.Allocate();
        if (!destinationTexture.descriptor.IsValid())
        {
            Diagnostics::Debug::LogError("D3D12 texture descriptor heap is full.");
            return false;
        }

        D3D12_RESOURCE_DESC textureDescription = {};
        textureDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDescription.Width = sourceTexture.width;
        textureDescription.Height = sourceTexture.height;
        textureDescription.DepthOrArraySize = 1;
        textureDescription.MipLevels = 1;
        textureDescription.Format = sourceTexture.format;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        const D3D12_HEAP_PROPERTIES defaultHeapProperties = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
        const HRESULT result = device.CreateCommittedResource(
            &defaultHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &textureDescription,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(destinationTexture.texture.ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            LogHResult("Creating the D3D12 texture", result);
            return false;
        }

        upload = RecordPixelUpload(
            device, commandList, uploadPool, sourceTexture, *destinationTexture.texture.Get());
        if (!upload)
        {
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC viewDescription = {};
        viewDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        viewDescription.Format = sourceTexture.format;
        viewDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        viewDescription.Texture2D.MipLevels = 1;
        device.CreateShaderResourceView(
            destinationTexture.texture.Get(),
            &viewDescription,
            descriptorAllocator.GetCpuHandle(destinationTexture.descriptor.GetIndex()));
        destinationTexture.revision = sourceTexture.revision;
        destinationTexture.width = sourceTexture.width;
        destinationTexture.height = sourceTexture.height;
        destinationTexture.format = sourceTexture.format;
        return true;
    }

    /// <summary>
    /// 이미 있는 텍스처에 새 revision의 픽셀을 다시 올린다. 셰이더 리소스 상태에서 복사 대상으로
    /// 내렸다가 되돌린다. 같은 큐에서 앞 프레임의 읽기가 먼저 실행되므로 순서는 큐가 지킨다.
    /// </summary>
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource> RecordPixelUpdate(
        ID3D12Device& device,
        ID3D12GraphicsCommandList& commandList,
        UploadPool& uploadPool,
        const D3D12ResolvedTexture& sourceTexture,
        D3D12TextureBinding& binding)
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> upload =
            RecordPixelUpload(device, commandList, uploadPool, sourceTexture, *binding.texture.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (upload)
        {
            binding.revision = sourceTexture.revision;
        }
        return upload;
    }
}

struct D3D12TextureUploader::Implementation
{
    ID3D12Device* device = nullptr;
    D3D12DescriptorHeapAllocator* descriptorAllocator = nullptr;

    /// <summary>
    /// GPU에 상주하는 바인딩들이다. 바이트는 공용 정책의 TextureBindingBudget이 정하고, 항목 수는
    /// descriptor 힙이 실제로 얻은 자리 수가 정한다 — 퇴거된 바인딩이 언제나 새 바인딩에게 자리
    /// 하나를 돌려주도록. 여기서는 정책이 원하는 수로 시작하고, Initialize가 allocator에게 물어
    /// 실제 수로 좁힌다.
    ///
    /// 항목은 in flight로 달리는 프레임 수만큼 보존한다: 두 프레임 전에 기록된 command list가 아직
    /// 실행 중일 수 있고, 그것이 참조하는 바인딩을 퇴거하면 GPU가 읽고 있는 텍스처를 해제하고 그
    /// 텍스처를 들여다보는 descriptor 자리를 재활용하는 셈이 된다.
    /// </summary>
    FrameBoundCache<std::uint64_t, D3D12TextureBinding> bindings{
        TextureBindingBudget.maximumEntries,
        TextureBindingBudget.maximumBytes,
        FrameSlotRetention };
    UploadPool uploadPool;
    std::unordered_set<std::uint64_t> failedSources;
    std::unordered_set<std::uint64_t> capacityRejectedSources;
    bool capacityWarningIssued = false;
    /// <summary>바인딩 캐시를 묶은 자리 수다. Initialize가 힙에게서 받아 적어 둔다.</summary>
    UINT descriptorCapacity = 0;
};

D3D12TextureUploader::D3D12TextureUploader()
    : mImplementation(std::make_unique<Implementation>())
{
}

D3D12TextureUploader::~D3D12TextureUploader() = default;

bool D3D12TextureUploader::Initialize(
    ID3D12Device& device,
    D3D12DescriptorHeapAllocator& descriptorAllocator)
{
    // 힙이 실제로 얻은 자리 수가 캐시의 항목 상한이다. 정책이 요구한 수를 그대로 믿지 않는 이유는
    // 힙이 정책과 장치 한계를 결합해 만들어지기 때문이다 — 장치가 더 좁으면 캐시는 allocator가
    // 줄 수 없는 자리가 있다고 믿게 되고, 그러면 텍스처가 그냥 나타나지 않는다.
    const UINT capacity = descriptorAllocator.GetCapacity();
    if (capacity == 0)
    {
        Diagnostics::Debug::LogError(
            "The D3D12 shader-visible heap holds no slots, so no texture could ever be bound.");
        return false;
    }
    mImplementation->device = &device;
    mImplementation->descriptorCapacity = capacity;
    mImplementation->bindings.SetMaximumEntries(capacity);
    mImplementation->descriptorAllocator = &descriptorAllocator;
    return true;
}

void D3D12TextureUploader::BeginFrame(const UINT frameIndex, const bool advanceCache)
{
    Implementation& data = *mImplementation;
    data.uploadPool.BeginFrame(frameIndex, advanceCache);
    if (advanceCache)
    {
        data.bindings.BeginFrame();
    }
    data.capacityRejectedSources.clear();
    data.capacityWarningIssued = false;
}

const D3D12TextureBinding* D3D12TextureUploader::Resolve(
    ID3D12GraphicsCommandList& commandList,
    const D3D12ResolvedTexture& source)
{
    Implementation& data = *mImplementation;
    if (!data.device || !data.descriptorAllocator || source.id == 0)
    {
        return nullptr;
    }
    if (D3D12TextureBinding* const cached = data.bindings.Find(source.id))
    {
        // 같은 id의 픽셀이 바뀌었으면 텍스처를 다시 만들지 않고 제자리에 다시 올린다. 크기가
        // 같다는 것은 계약이다: 크기가 바뀌면 새 id다.
        if (cached->revision != source.revision && cached->texture &&
            cached->width == source.width && cached->height == source.height &&
            cached->format == source.format && source.IsValid())
        {
            static_cast<void>(RecordPixelUpdate(
                *data.device, commandList, data.uploadPool, source, *cached));
        }
        return cached;
    }
    if (data.failedSources.contains(source.id) || data.capacityRejectedSources.contains(source.id))
    {
        return nullptr;
    }

    // Reserve cache capacity before uploading so eviction can free a descriptor slot for it.
    const std::size_t byteSize = source.GetPixelByteSize();
    if (!data.bindings.MakeRoom(byteSize))
    {
        data.capacityRejectedSources.insert(source.id);
        if (!data.capacityWarningIssued)
        {
            Diagnostics::Debug::LogWarning(
                "D3D12 texture binding cache reached its descriptor or memory limit.");
            data.capacityWarningIssued = true;
        }
        return nullptr;
    }

    D3D12TextureBinding binding;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    if (!UploadTexture(
            *data.device, *data.descriptorAllocator, commandList, data.uploadPool,
            source, binding, upload))
    {
        if (data.failedSources.size() >= data.descriptorCapacity)
        {
            data.failedSources.clear();
        }
        data.failedSources.insert(source.id);
        return nullptr;
    }

    const D3D12TextureBinding* const cached =
        data.bindings.Insert(source.id, std::move(binding), byteSize);
    if (!cached)
    {
        Diagnostics::Debug::LogError("D3D12 texture binding cache rejected a duplicate key.");
        return nullptr;
    }
    // The source keeps its pixels: this binding can be evicted while the resolver still caches the
    // source, and the only way back is to upload the same pixels again.
    return cached;
}

}
