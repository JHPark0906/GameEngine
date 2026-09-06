#include "pch.h"
#include "D3D12FrameResources.h"

#include "../RenderResourceCachePolicy.h"
#include "../RenderSubmissionPolicy.h"


#include <windows.h>
#include <cstddef>
#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <memory>
#include <variant>
#include <limits>
#include <utility>
#include <vector>
#include <wrl/client.h>

#pragma comment(lib, "d3d12.lib")

#include "D3D12ResourceDescriptions.h"
#include "../../Platform/Win32/Win32Diagnostics.h"
#include "../ShaderInterop.h"
#include "../RenderSamplerPolicy.h"
#include "../ShaderBindings.h"

namespace GameEngine::Rendering::D3D12
{

using Platform::Win32::LogHResult;

UINT ComputeShaderVisibleDescriptorCount(ID3D12Device& device)
{
    // 장치가 줄 수 있는 자리 수는 resource binding tier가 정한다. 물어보지 못하면 가장 좁은
    // tier를 가정한다 — 장치가 못 하는 것을 요구하느니 덜 쓰는 편이 안전하다. 지금 SDK에서는 두
    // tier의 상한 값이 같지만, 한계는 tier에서 읽는 것이 옳다: 값이 같다는 것은 이 API가 지금
    // 그렇다는 사실일 뿐 우리가 기대야 할 규칙이 아니다.
    UINT deviceLimit = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    if (SUCCEEDED(device.CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) &&
        options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2)
    {
        deviceLimit = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2;
    }
    return CombineDescriptorCount(TextureBindingBudget.maximumEntries, deviceLimit);
}

namespace
{
    [[nodiscard]] constexpr std::size_t AlignedConstantBytes(const std::size_t bytes)
    {
        constexpr std::size_t alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        return (bytes + alignment - 1) / alignment * alignment;
    }

    /// <summary>공용 샘플러 정책을 이 API의 정적 샘플러로 번역한다.</summary>
    [[nodiscard]] D3D12_STATIC_SAMPLER_DESC MakeStaticSampler(const PipelineKind kind)
    {
        const D3D12_TEXTURE_ADDRESS_MODE addressMode =
            GetSamplerAddressMode(kind) == SamplerAddressMode::Wrap
                ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = addressMode;
        sampler.AddressV = addressMode;
        sampler.AddressW = addressMode;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = GetShaderBindings(kind).sampler;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        return sampler;
    }

    static_assert(
        AlignedConstantBytes(sizeof(SpriteConstants)) == AlignedConstantBytes(sizeof(TextConstants)));
}

std::size_t ComputeFrameConstantBytes(const RenderFrame& frame)
{
    std::size_t meshes = 0;
    std::size_t skinnedMeshes = 0;
    std::size_t quads = 0;
    for (std::size_t pass = 0; pass < static_cast<std::size_t>(RenderPass::Count); ++pass)
    for (const DrawPacket& packet : frame.GetDrawPackets(static_cast<RenderPass>(pass)))
    {
        std::size_t addedQuads = 0;
        if (std::holds_alternative<MeshDraw>(packet.payload)) ++meshes;
        else if (std::holds_alternative<SkinnedMeshDraw>(packet.payload)) ++skinnedMeshes;
        else if (const auto* sprite = std::get_if<SpriteDraw>(&packet.payload))
            addedQuads = sprite->sliced ? 9 : 1;
        else if (const auto* text = std::get_if<TextDraw>(&packet.payload))
            addedQuads = text->glyphs ? text->glyphs->size() : 0;
        else if (const auto* tilemap = std::get_if<TilemapDraw>(&packet.payload))
            addedQuads = tilemap->tiles ? tilemap->tiles->size() : 0;
        quads += (std::min)(addedQuads, FrameSubmissionBudget.maximumQuadsPerFrame - quads);
    }
    skinnedMeshes = (std::min)(skinnedMeshes, FrameSubmissionBudget.maximumSkinnedMeshesPerFrame);
    return (meshes + skinnedMeshes) * AlignedConstantBytes(sizeof(MeshConstants)) +
        quads * AlignedConstantBytes(sizeof(SpriteConstants));
}

struct D3D12FrameResources::Implementation
{
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shaderResourceHeap;
    struct ConstantStorage
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
        std::byte* mapped = nullptr;
        std::size_t capacity = 0;
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> retired;
        ~ConstantStorage() { if (buffer && mapped) buffer->Unmap(0, nullptr); }
    };
    std::array<ConstantStorage, FrameSlotCount> constants;
    ID3D12Device* device = nullptr;
    UINT currentSlot = 0;
    std::size_t constantBufferOffset = 0;
    mutable D3D12DescriptorHeapAllocator descriptorAllocator;
    D3D12_GPU_DESCRIPTOR_HANDLE descriptorHeapStart{};
    UINT descriptorStride = 0;
    ID3D12GraphicsCommandList* textureCommandList = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE boundTexture{};
    bool constantOverflowReported = false;

};

D3D12FrameResources::D3D12FrameResources()
    : mImplementation(std::make_unique<Implementation>())
{
}

D3D12FrameResources::~D3D12FrameResources() = default;

bool D3D12FrameResources::Initialize(ID3D12Device& device)
{
    Implementation& data = *mImplementation;

    // One CBV for the draw's constants and one SRV table for its texture. Every pipeline shares this
    // signature, so a pass only has to select its pipeline state.
    //
    // The slots come from the shared binding table rather than from literals here, because the same
    // numbers appear in the shaders and there is no way to notice by looking that two of them have
    // drifted apart. Every program binds through the same slots, so the mesh program stands for all
    // three; only the samplers differ, and both of those are declared below.
    constexpr ShaderBindingSlots slots = GetShaderBindings(ShaderProgram::Mesh);
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = slots.texture;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = slots.constantBuffer;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // Every pipeline shares this root signature, so a pipeline cannot bind a sampler of its own.
    // It selects the address mode the shared policy prescribes by sampling through that kind's
    // slot instead, which is why both samplers are declared here.
    const D3D12_STATIC_SAMPLER_DESC samplers[] = {
        MakeStaticSampler(PipelineKind::Mesh),
        MakeStaticSampler(PipelineKind::Sprite),
    };
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDescription = {};
    rootSignatureDescription.NumParameters = static_cast<UINT>(std::size(parameters));
    rootSignatureDescription.pParameters = parameters;
    rootSignatureDescription.NumStaticSamplers = static_cast<UINT>(std::size(samplers));
    rootSignatureDescription.pStaticSamplers = samplers;
    rootSignatureDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSignature;
    Microsoft::WRL::ComPtr<ID3DBlob> rootSignatureErrors;
    HRESULT result = D3D12SerializeRootSignature(
        &rootSignatureDescription,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSignature.ReleaseAndGetAddressOf(),
        rootSignatureErrors.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        if (rootSignatureErrors)
        {
            OutputDebugStringA(static_cast<const char*>(rootSignatureErrors->GetBufferPointer()));
        }
        LogHResult("Serializing the D3D12 root signature", result);
        return false;
    }
    result = device.CreateRootSignature(
        0,
        serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(),
        IID_PPV_ARGS(data.rootSignature.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating the D3D12 root signature", result);
        return false;
    }

    const UINT descriptorCount = ComputeShaderVisibleDescriptorCount(device);
    D3D12_DESCRIPTOR_HEAP_DESC heapDescription = {};
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDescription.NumDescriptors = descriptorCount;
    heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    result = device.CreateDescriptorHeap(
        &heapDescription, IID_PPV_ARGS(data.shaderResourceHeap.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating the D3D12 shader-visible descriptor heap", result);
        return false;
    }
    data.descriptorStride = device.GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    data.descriptorHeapStart = data.shaderResourceHeap->GetGPUDescriptorHandleForHeapStart();
    data.descriptorAllocator.Configure(
        *data.shaderResourceHeap.Get(), data.descriptorStride, descriptorCount);
    InvalidateTextureBinding();

    data.device = &device;
    return true;
}

void D3D12FrameResources::BeginFrame(const UINT frameIndex)
{
    // This frame slot owns one segment of the ring. The caller has already waited for the fence
    // belonging to this slot, so the GPU has finished with whatever was written here last time.
    mImplementation->currentSlot = frameIndex % FrameSlotCount;
    mImplementation->constants[mImplementation->currentSlot].retired.clear();
    mImplementation->constantBufferOffset = 0;
    mImplementation->constantOverflowReported = false;
    InvalidateTextureBinding();
}

bool D3D12FrameResources::PrepareFrame(const RenderFrame& frame)
{
    Implementation& data = *mImplementation;
    auto& storage = data.constants[data.currentSlot];
    const std::size_t required = data.constantBufferOffset + ComputeFrameConstantBytes(frame);
    if (required <= storage.capacity) return true;
    if (!data.device) return false;
    // Round up to a 64 KiB allocation boundary. This slot's fence has completed, so its old
    // buffer may be replaced; every other in-flight slot keeps its own buffer and mapping.
    constexpr std::size_t growthAlignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    if (required > (std::numeric_limits<std::size_t>::max)() - growthAlignment) return false;
    const std::size_t capacity = (required + growthAlignment - 1) / growthAlignment * growthAlignment;
    const auto heap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const auto description = BufferDescription(capacity);
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    HRESULT result = data.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(buffer.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Growing the D3D12 frame constant buffer", result);
        return false;
    }
    std::byte* mapped = nullptr;
    const D3D12_RANGE noRead{ 0, 0 };
    result = buffer->Map(0, &noRead, reinterpret_cast<void**>(&mapped));
    if (FAILED(result))
    {
        LogHResult("Mapping the D3D12 frame constant buffer", result);
        return false;
    }
    if (storage.buffer && storage.mapped) storage.buffer->Unmap(0, nullptr);
    if (data.constantBufferOffset != 0) storage.retired.push_back(storage.buffer);
    storage.buffer = std::move(buffer);
    storage.gpuAddress = storage.buffer->GetGPUVirtualAddress();
    storage.mapped = mapped;
    storage.capacity = capacity;
    return true;
}

void D3D12FrameResources::BindFrameState(ID3D12GraphicsCommandList& commandList) const
{
    ID3D12DescriptorHeap* heaps[] = { mImplementation->shaderResourceHeap.Get() };
    commandList.SetDescriptorHeaps(1, heaps);
    InvalidateTextureBinding();
}

void D3D12FrameResources::InvalidateTextureBinding() const
{
    mImplementation->textureCommandList = nullptr;
}

ID3D12RootSignature* D3D12FrameResources::GetRootSignature() const
{
    return mImplementation->rootSignature.Get();
}

D3D12DescriptorHeapAllocator& D3D12FrameResources::GetDescriptorAllocator() const
{
    return mImplementation->descriptorAllocator;
}

bool D3D12FrameResources::BindDrawConstantData(
    ID3D12GraphicsCommandList& commandList,
    const void* constants,
    const std::size_t byteSize,
    const UINT textureDescriptorIndex)
{
    Implementation& data = *mImplementation;
    auto& storage = data.constants[data.currentSlot];
    if (!storage.mapped || !storage.buffer)
    {
        return false;
    }
    const std::size_t stride = AlignedConstantBytes(byteSize);
    if (stride > storage.capacity - data.constantBufferOffset)
    {
        if (!data.constantOverflowReported)
        {
            Diagnostics::Debug::LogWarning("D3D12 frame constant buffer is full for this frame.");
            data.constantOverflowReported = true;
        }
        return false;
    }

    const std::size_t offset = data.constantBufferOffset;
    std::memcpy(storage.mapped + offset, constants, byteSize);
    commandList.SetGraphicsRootConstantBufferView(
        0, storage.gpuAddress + offset);
    D3D12_GPU_DESCRIPTOR_HANDLE texture = data.descriptorHeapStart;
    texture.ptr += static_cast<UINT64>(textureDescriptorIndex) * data.descriptorStride;
    if (data.textureCommandList != &commandList || data.boundTexture.ptr != texture.ptr)
    {
        commandList.SetGraphicsRootDescriptorTable(1, texture);
        data.textureCommandList = &commandList;
        data.boundTexture = texture;
    }
    data.constantBufferOffset += stride;
    return true;
}

}
