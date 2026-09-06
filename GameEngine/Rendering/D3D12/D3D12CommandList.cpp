#include "pch.h"
#include "D3D12CommandList.h"


#include <windows.h>
#include <cstddef>
#include <cstring>
#include <span>
#include <iterator>
#include <memory>
#include <optional>
#include <wrl/client.h>

#include "D3D12FrameResources.h"
#include "D3D12MeshUploader.h"
#include "D3D12PipelineDescriptions.h"
#include "D3D12ResolvedResources.h"
#include "D3D12ResourceDescriptions.h"
#include "D3D12SkinnedMeshUploader.h"
#include "D3D12TextureUploader.h"
#include "ID3D12GraphicsDevice.h"
#include "../../Platform/Win32/Win32Diagnostics.h"
#include "../Direct3D/MatrixConversion.h"
#include "../Direct3D/ShaderCompiler.h"
#include "../MeshDrawGeometry.h"
#include "../RenderSamplerPolicy.h"
#include "../RenderSubmissionPolicy.h"
#include "../ShaderBindings.h"
#include "../ShaderInterop.h"
#include "../Direct3D/ShaderPaths.h"

namespace GameEngine::Rendering::D3D12
{

using Platform::Win32::LogHResult;
using Direct3D::StoreTransposed;

namespace
{
    constexpr UINT SpriteQuadVertexCount = static_cast<UINT>(SpriteQuadVertices.size());
    constexpr UINT BoneConstantStride = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT *
        ((sizeof(BoneConstants) + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) /
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    constexpr UINT BoneBufferSizePerFrame = BoneConstantStride *
        static_cast<UINT>(FrameSubmissionBudget.maximumSkinnedMeshesPerFrame);
}

struct D3D12CommandList::Implementation
{
    D3D12FrameResources* frameResources = nullptr;
    D3D12MeshUploader* meshUploader = nullptr;
    D3D12SkinnedMeshUploader* skinnedMeshUploader = nullptr;
    D3D12TextureUploader* textureUploader = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    bool missingCommandListReported = false;

    /// <summary>
    /// BeginFrame이 기록 중인 command list를 가리키게 하기 전에 도착한 draw를 보고한다. draw마다
    /// 가 아니라 프레임당 한 번 보고하는데, 이 조건은 프레임의 모든 draw를 거부해서 로그를
    /// 파묻어 버릴 것이기 때문이다. 아무것도 조용히 그리지 않는 것이 바로 이것이 잡는 실패다.
    /// </summary>
    [[nodiscard]] bool HasCommandList()
    {
        if (commandList)
        {
            return true;
        }
        if (!missingCommandListReported)
        {
            Diagnostics::Debug::LogError(
                "The D3D12 command list recorded a draw before BeginFrame supplied a command list. "
                "The frame will draw nothing.");
            missingCommandListReported = true;
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> meshPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blendedMeshPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spritePipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> textPipelineState;
    Microsoft::WRL::ComPtr<ID3D12Resource> quadVertexBuffer;

    // Skinned mesh pipeline. 공용 root signature를 쓰지 않고 자기 것을 따로 갖는다: 뼈 행렬을
    // 위한 세 번째 root parameter(CBV, b1)가 필요한데 그것은 Mesh·Sprite·Text 어느 것도 쓰지
    // 않는 자리라, 공용 서명에 얹으면 그 셋이 안 쓰는 슬롯을 영구히 안고 간다.
    Microsoft::WRL::ComPtr<ID3D12RootSignature> skinnedMeshRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skinnedMeshPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blendedSkinnedMeshPipelineState;
    /// <summary>
    /// 뼈 행렬 상수의 프레임별 링이다. D3D12FrameResources의 상수 링과 같은 이유로 존재한다 —
    /// 이 command list가 별도 root signature를 쓰므로 그 링을 통해 뼈 행렬을 올릴 수 없다.
    /// 세그먼트는 D3D12CommandList::BeginFrame이 받는 frameIndex로 고른다.
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12Resource> boneConstantBuffer;
    std::byte* mappedBoneConstants = nullptr;
    UINT boneConstantBufferOffset = 0;
    UINT boneConstantBufferSegmentEnd = 0;
    bool boneConstantOverflowReported = false;

    /// <summary>
    /// 이 command list가 현재 쥔 파이프라인이다. 리스트를 리셋하면 GPU 상태가 지워지므로
    /// BeginFrame이 이것도 함께 지운다. 한 프레임 안에서 파이프라인을 공유하는 연속된 draw는 한
    /// 번만 바인딩한다.
    /// </summary>
    std::optional<PipelineKind> boundPipeline;
    bool boundAlphaBlended = false;

    /// <summary>
    /// 파이프라인을 고르고, quad 파이프라인이면 공용 quad 정점 버퍼도 함께 바인딩한다. 둘 다
    /// 바뀔 때만 일어난다. 프레임의 draw가 파이프라인별로 묶여 도착하기 때문이다.
    /// </summary>
    void BindPipeline(const PipelineKind kind, const bool alphaBlended = false)
    {
        if (boundPipeline == kind && boundAlphaBlended == alphaBlended)
        {
            return;
        }
        boundPipeline = kind;
        boundAlphaBlended = alphaBlended;

        ID3D12PipelineState* pipelineState =
            alphaBlended ? blendedMeshPipelineState.Get() : meshPipelineState.Get();
        if (kind == PipelineKind::Sprite) pipelineState = spritePipelineState.Get();
        else if (kind == PipelineKind::Text) pipelineState = textPipelineState.Get();
        else if (kind == PipelineKind::SkinnedMesh)
        {
            pipelineState = alphaBlended ? blendedSkinnedMeshPipelineState.Get() :
                skinnedMeshPipelineState.Get();
        }

        commandList->SetPipelineState(pipelineState);
        // 스킨드 메시만 자기 root signature를 쓴다 — 뼈 행렬을 위한 세 번째 root parameter가
        // 그 이유다(위 멤버 선언의 주석 참고). 매개변수 0·1의 모양은 공용 서명과 같으므로,
        // frameResources->BindDrawConstants는 어느 쪽이 바인딩돼 있어도 그대로 통한다.
        frameResources->InvalidateTextureBinding();
        commandList->SetGraphicsRootSignature(
            kind == PipelineKind::SkinnedMesh
                ? skinnedMeshRootSignature.Get()
                : frameResources->GetRootSignature());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        if (kind != PipelineKind::Mesh && kind != PipelineKind::SkinnedMesh)
        {
            // A mesh draw binds its own vertex buffer, so the quad buffer has to be rebound when
            // coming back to a quad pipeline — which is exactly when the pipeline changes.
            D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
            vertexBufferView.BufferLocation = quadVertexBuffer->GetGPUVirtualAddress();
            vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(SpriteQuadVertices));
            vertexBufferView.StrideInBytes = sizeof(SpriteVertex);
            commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
        }
    }

    /// <summary>텍스처를 처음 쓸 때 업로드하고, 그것에 바인딩된 descriptor 슬롯을 반환한다.</summary>
    [[nodiscard]] bool TryResolveTextureDescriptor(
        const D3D12ResolvedTexture& texture, UINT& descriptorIndex) const
    {
        const D3D12TextureBinding* const binding =
            textureUploader->Resolve(*commandList, texture);
        if (!binding)
        {
            return false;
        }
        descriptorIndex = binding->descriptor.GetIndex();
        return true;
    }

    /// <summary>
    /// 뼈 행렬 상수 링에 정렬된 슬롯 하나를 예약하고 root parameter 2에 바인딩한다. 링이
    /// 소진되면 false를 반환하며, 그 경우 아무것도 바인딩되지 않는다.
    /// </summary>
    [[nodiscard]] bool BindBoneConstants(const BoneConstants& constants)
    {
        constexpr UINT stride = BoneConstantStride;
        if (!mappedBoneConstants || !boneConstantBuffer)
        {
            return false;
        }
        if (boneConstantBufferOffset + stride > boneConstantBufferSegmentEnd)
        {
            if (!boneConstantOverflowReported)
            {
                Diagnostics::Debug::LogWarning("D3D12 bone constant ring is full for this frame.");
                boneConstantOverflowReported = true;
            }
            return false;
        }
        const UINT offset = boneConstantBufferOffset;
        std::memcpy(mappedBoneConstants + offset, &constants, sizeof(constants));
        commandList->SetGraphicsRootConstantBufferView(
            2, boneConstantBuffer->GetGPUVirtualAddress() + offset);
        boneConstantBufferOffset += stride;
        return true;
    }

    [[nodiscard]] bool InitializeMeshPipeline(ID3D12Device& device);
    [[nodiscard]] bool InitializeSkinnedMeshPipeline(ID3D12Device& device);
    [[nodiscard]] bool InitializeQuadPipelines(ID3D12Device& device);
};

bool D3D12CommandList::Implementation::InitializeMeshPipeline(ID3D12Device& device)
{
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    if (!Direct3D::CompileShader(ShaderProgram::Mesh, "VS", "vs_5_0", vertexShader) ||
        !Direct3D::CompileShader(ShaderProgram::Mesh, "PS", "ps_5_0", pixelShader))
    {
        return false;
    }

    const D3D12_INPUT_ELEMENT_DESC inputElements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshVertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(MeshVertex, textureCoordinate), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline =
        MakeGraphicsPipelineDescription(*frameResources->GetRootSignature());
    pipeline.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    pipeline.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    pipeline.InputLayout = { inputElements, static_cast<UINT>(std::size(inputElements)) };
    // Opaque geometry is the only stage that writes depth and rejects back faces.
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pipeline.DepthStencilState.DepthEnable = TRUE;
    pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pipeline.BlendState.RenderTarget[0].BlendEnable = FALSE;

    HRESULT result = device.CreateGraphicsPipelineState(
        &pipeline, IID_PPV_ARGS(meshPipelineState.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating the D3D12 mesh pipeline state", result);
        return false;
    }
    pipeline.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    result = device.CreateGraphicsPipelineState(
        &pipeline, IID_PPV_ARGS(blendedMeshPipelineState.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating the D3D12 blended mesh pipeline state", result);
        return false;
    }
    return true;
}

bool D3D12CommandList::Implementation::InitializeSkinnedMeshPipeline(ID3D12Device& device)
{
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    if (!Direct3D::CompileShader(ShaderProgram::SkinnedMesh, "VS", "vs_5_0", vertexShader) ||
        !Direct3D::CompileShader(ShaderProgram::SkinnedMesh, "PS", "ps_5_0", pixelShader))
    {
        return false;
    }

    // 매개변수 0·1은 공용 root signature와 같은 모양(CBV, 서술자 테이블)이라 frameResources의
    // BindDrawConstants를 그대로 쓸 수 있다. 매개변수 2가 이 서명만의 것 — 뼈 행렬 CBV — 이다.
    constexpr ShaderBindingSlots slots = GetShaderBindings(ShaderProgram::SkinnedMesh);
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = slots.texture;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER parameters[3] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = slots.constantBuffer;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[2].Descriptor.ShaderRegister = SkinnedMeshBoneConstantBufferSlot;
    // 정점 셰이더만 뼈 행렬을 읽는다.
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    const D3D12_TEXTURE_ADDRESS_MODE addressMode =
        GetSamplerAddressMode(PipelineKind::SkinnedMesh) == SamplerAddressMode::Wrap
            ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
            : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = addressMode;
    sampler.AddressV = addressMode;
    sampler.AddressW = addressMode;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = slots.sampler;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDescription = {};
    rootSignatureDescription.NumParameters = static_cast<UINT>(std::size(parameters));
    rootSignatureDescription.pParameters = parameters;
    rootSignatureDescription.NumStaticSamplers = 1;
    rootSignatureDescription.pStaticSamplers = &sampler;
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
        LogHResult("Serializing the D3D12 skinned mesh root signature", result);
        return false;
    }
    result = device.CreateRootSignature(
        0,
        serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(),
        IID_PPV_ARGS(skinnedMeshRootSignature.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating the D3D12 skinned mesh root signature", result);
        return false;
    }

    const D3D12_INPUT_ELEMENT_DESC inputElements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SkinnedMeshVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SkinnedMeshVertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SkinnedMeshVertex, textureCoordinate), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, offsetof(SkinnedMeshVertex, boneIndices), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(SkinnedMeshVertex, boneWeights), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline =
        MakeGraphicsPipelineDescription(*skinnedMeshRootSignature.Get());
    pipeline.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    pipeline.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    pipeline.InputLayout = { inputElements, static_cast<UINT>(std::size(inputElements)) };
    // 정적 메시와 같은 이유로 같은 opaque 설정이다: 불투명 스키닝 메시도 뒷면을 그리지 않고
    // 깊이를 쓴다.
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pipeline.DepthStencilState.DepthEnable = TRUE;
    pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pipeline.BlendState.RenderTarget[0].BlendEnable = FALSE;

    result = device.CreateGraphicsPipelineState(
        &pipeline, IID_PPV_ARGS(skinnedMeshPipelineState.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating the D3D12 skinned mesh pipeline state", result);
        return false;
    }
    pipeline.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    result = device.CreateGraphicsPipelineState(
        &pipeline, IID_PPV_ARGS(blendedSkinnedMeshPipelineState.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating the D3D12 blended skinned mesh pipeline state", result);
        return false;
    }

    // 뼈 행렬 상수 링이다. frameResources의 링과 같은 모양이지만, 이 command list만의 root
    // signature를 쓰므로 여기서 따로 갖는다. 공통 스킨드 제출 상한만큼 각 프레임 슬롯에
    // 공간을 확보하여 다른 포즈의 draw들이 같은 상수를 덮어쓰지 않게 한다.
    constexpr UINT boneBufferSize = BoneBufferSizePerFrame * FrameSlotCount;
    const D3D12_HEAP_PROPERTIES uploadHeapProperties = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC boneBufferDescription = BufferDescription(boneBufferSize);
    result = device.CreateCommittedResource(
        &uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
        &boneBufferDescription, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(boneConstantBuffer.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating the D3D12 bone constant buffer", result);
        return false;
    }
    result = boneConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedBoneConstants));
    if (FAILED(result))
    {
        LogHResult("Mapping the D3D12 bone constant buffer", result);
        return false;
    }
    return true;
}

bool D3D12CommandList::Implementation::InitializeQuadPipelines(ID3D12Device& device)
{
    const D3D12_INPUT_ELEMENT_DESC inputElements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SpriteVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SpriteVertex, textureCoordinate), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    Microsoft::WRL::ComPtr<ID3DBlob> spriteVertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> spritePixelShader;
    if (!Direct3D::CompileShader(ShaderProgram::Sprite, "VS", "vs_5_0", spriteVertexShader) ||
        !Direct3D::CompileShader(ShaderProgram::Sprite, "PS", "ps_5_0", spritePixelShader))
    {
        return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC spritePipeline =
        MakeGraphicsPipelineDescription(*frameResources->GetRootSignature());
    spritePipeline.VS = { spriteVertexShader->GetBufferPointer(), spriteVertexShader->GetBufferSize() };
    spritePipeline.PS = { spritePixelShader->GetBufferPointer(), spritePixelShader->GetBufferSize() };
    spritePipeline.InputLayout = { inputElements, static_cast<UINT>(std::size(inputElements)) };
    HRESULT result = device.CreateGraphicsPipelineState(
        &spritePipeline, IID_PPV_ARGS(spritePipelineState.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating the D3D12 sprite pipeline state", result);
        return false;
    }

    Microsoft::WRL::ComPtr<ID3DBlob> textVertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> textPixelShader;
    if (!Direct3D::CompileShader(ShaderProgram::Text, "VS", "vs_5_0", textVertexShader) ||
        !Direct3D::CompileShader(ShaderProgram::Text, "PS", "ps_5_0", textPixelShader))
    {
        return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC textPipeline = spritePipeline;
    textPipeline.VS = { textVertexShader->GetBufferPointer(), textVertexShader->GetBufferSize() };
    textPipeline.PS = { textPixelShader->GetBufferPointer(), textPixelShader->GetBufferSize() };
    result = device.CreateGraphicsPipelineState(
        &textPipeline, IID_PPV_ARGS(textPipelineState.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating the D3D12 text pipeline state", result);
        return false;
    }

    const D3D12_HEAP_PROPERTIES uploadHeapProperties = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC vertexBufferDescription =
        BufferDescription(sizeof(SpriteQuadVertices));
    result = device.CreateCommittedResource(
        &uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
        &vertexBufferDescription, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(quadVertexBuffer.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating the D3D12 sprite quad vertex buffer", result);
        return false;
    }
    std::byte* mappedVertices = nullptr;
    result = quadVertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertices));
    if (FAILED(result))
    {
        LogHResult("Mapping the D3D12 sprite quad vertex buffer", result);
        return false;
    }
    std::memcpy(
        mappedVertices, SpriteQuadVertices.data(), sizeof(SpriteQuadVertices));
    quadVertexBuffer->Unmap(0, nullptr);
    return true;
}

D3D12CommandList::D3D12CommandList()
    : mImplementation(std::make_unique<Implementation>())
{
}

D3D12CommandList::~D3D12CommandList() = default;

bool D3D12CommandList::Initialize(
    ID3D12GraphicsDevice& graphicsDevice,
    D3D12FrameResources& frameResources,
    D3D12MeshUploader& meshUploader,
    D3D12SkinnedMeshUploader& skinnedMeshUploader,
    D3D12TextureUploader& textureUploader)
{
    ID3D12Device* const device = graphicsDevice.GetDevice();
    if (!device)
    {
        Diagnostics::Debug::LogError("A valid D3D12 device is required by the command list.");
        return false;
    }
    if (!frameResources.GetRootSignature())
    {
        Diagnostics::Debug::LogError(
            "An initialized root signature is required by the D3D12 command list.");
        return false;
    }
    mImplementation->frameResources = &frameResources;
    mImplementation->meshUploader = &meshUploader;
    mImplementation->skinnedMeshUploader = &skinnedMeshUploader;
    mImplementation->textureUploader = &textureUploader;

    return mImplementation->InitializeMeshPipeline(*device) &&
        mImplementation->InitializeSkinnedMeshPipeline(*device) &&
        mImplementation->InitializeQuadPipelines(*device);
}

void D3D12CommandList::BeginFrame(const UINT frameIndex, ID3D12GraphicsCommandList& commandList)
{
    mImplementation->commandList = &commandList;
    mImplementation->missingCommandListReported = false;
    // Resetting the command list cleared the GPU state, so nothing is bound.
    mImplementation->boundPipeline.reset();
    if (mImplementation->frameResources)
    {
        mImplementation->frameResources->InvalidateTextureBinding();
    }

    // 뼈 행렬 링의 세그먼트다. D3D12FrameResources::BeginFrame과 같은 규칙 — 주 프레임과 지연
    // 캡처 채널마다 다른 frameIndex가 오므로 서로 겹치지 않는 세그먼트를 받는다.
    const UINT segment = frameIndex % FrameSlotCount;
    mImplementation->boneConstantBufferOffset = segment * BoneBufferSizePerFrame;
    mImplementation->boneConstantBufferSegmentEnd =
        mImplementation->boneConstantBufferOffset + BoneBufferSizePerFrame;
    mImplementation->boneConstantOverflowReported = false;
}

const char* D3D12CommandList::GetBackendName() const
{
    return "D3D12";
}

bool D3D12CommandList::DrawMesh(
    const MeshShading& transform,
    const IResolvedGeometry& geometry,
    const IResolvedTexture& material)
{
    Implementation& data = *mImplementation;
    if (!data.HasCommandList())
    {
        return false;
    }
    if (!Accepts(geometry) || !Accepts(material))
    {
        Diagnostics::Debug::LogError(
            "The D3D12 command list was handed a resolved resource from another backend.");
        return false;
    }
    const auto& meshGeometry = static_cast<const D3D12ResolvedGeometry&>(geometry);
    const auto& meshMaterial = static_cast<const D3D12ResolvedTexture&>(material);

    const D3D12MeshBinding* const meshBinding =
        data.meshUploader->Resolve(*data.commandList, *meshGeometry.mesh);
    if (!meshBinding)
    {
        return false;
    }

    UINT textureDescriptorIndex = 0;
    if (!data.TryResolveTextureDescriptor(meshMaterial, textureDescriptorIndex))
    {
        return false;
    }

    data.BindPipeline(PipelineKind::Mesh, transform.alphaBlended);

    MeshConstants constants;
    StoreTransposed(constants.worldViewProjection, transform.worldViewProjection);
    StoreTransposed(constants.world, transform.world);
    StoreTransposed(constants.normalToWorld, transform.normalToWorld);
    StoreMeshLighting(transform, constants);
    if (!data.frameResources->BindDrawConstants(
            *data.commandList, constants, textureDescriptorIndex))
    {
        return false;
    }

    data.commandList->IASetVertexBuffers(0, 1, &meshBinding->vertexBufferView);
    data.commandList->IASetIndexBuffer(&meshBinding->indexBufferView);
    data.commandList->DrawIndexedInstanced(meshBinding->indexCount, 1, 0, 0, 0);
    return true;
}

bool D3D12CommandList::DrawSkinnedMesh(
    const MeshShading& transform,
    const IResolvedGeometry& geometry,
    const IResolvedTexture& material,
    const std::span<const Math::Matrix4x4> boneMatrices)
{
    Implementation& data = *mImplementation;
    if (!data.HasCommandList())
    {
        return false;
    }
    if (!Accepts(geometry) || !Accepts(material))
    {
        Diagnostics::Debug::LogError(
            "The D3D12 command list was handed a resolved resource from another backend.");
        return false;
    }
    if (boneMatrices.size() > MaxSkinnedMeshBones)
    {
        Diagnostics::Debug::LogError(
            "A skinned mesh draw named more bones than the shader can hold. count=",
            boneMatrices.size(), ", max=", MaxSkinnedMeshBones);
        return false;
    }
    const auto& skinnedGeometry = static_cast<const D3D12ResolvedSkinnedGeometry&>(geometry);
    const auto& meshMaterial = static_cast<const D3D12ResolvedTexture&>(material);

    const D3D12SkinnedMeshBinding* const meshBinding =
        data.skinnedMeshUploader->Resolve(*data.commandList, *skinnedGeometry.mesh);
    if (!meshBinding)
    {
        return false;
    }

    UINT textureDescriptorIndex = 0;
    if (!data.TryResolveTextureDescriptor(meshMaterial, textureDescriptorIndex))
    {
        return false;
    }

    data.BindPipeline(PipelineKind::SkinnedMesh, transform.alphaBlended);

    MeshConstants constants;
    StoreTransposed(constants.worldViewProjection, transform.worldViewProjection);
    StoreTransposed(constants.world, transform.world);
    StoreTransposed(constants.normalToWorld, transform.normalToWorld);
    StoreMeshLighting(transform, constants);
    // 매개변수 0·1을 공용 root signature와 같은 모양으로 뒀으므로, 지금 스킨드 메시 서명이
    // 바인딩돼 있어도 이 호출은 그대로 통한다 — 실제 CBV·서술자 테이블 바인딩 호출은 파라미터
    // 인덱스만 보고, 어느 root signature 객체가 지금 활성인지는 신경 쓰지 않는다.
    if (!data.frameResources->BindDrawConstants(
            *data.commandList, constants, textureDescriptorIndex))
    {
        return false;
    }

    BoneConstants boneConstants;
    for (std::size_t index = 0; index < boneMatrices.size(); ++index)
    {
        StoreTransposed(boneConstants.boneMatrices[index], boneMatrices[index]);
    }
    if (!data.BindBoneConstants(boneConstants))
    {
        return false;
    }

    data.commandList->IASetVertexBuffers(0, 1, &meshBinding->vertexBufferView);
    data.commandList->IASetIndexBuffer(&meshBinding->indexBufferView);
    data.commandList->DrawIndexedInstanced(meshBinding->indexCount, 1, 0, 0, 0);
    return true;
}

bool D3D12CommandList::DrawQuad(
    const PipelineKind pipelineKind,
    const QuadTransform& transform,
    const IResolvedTexture& texture)
{
    // quad 하나는 크기 1짜리 배치로 제출해 단일 draw와 배치가 같은 경로를 사용한다.
    return DrawQuads(pipelineKind, std::span{ &transform, 1 }, texture);
}
bool D3D12CommandList::DrawQuads(
    const PipelineKind pipelineKind,
    const std::span<const QuadTransform> transforms,
    const IResolvedTexture& texture)
{
    // D3D12는 draw마다 프레임 상수 링에서 슬롯 하나를 쓰므로 그 부분은 quad마다 남는다. 배치
    // 전체가 함께 보는 것 — 어느 백엔드의 텍스처인지, 파이프라인, 그리고 텍스처 디스크립터 —
    // 은 고리 밖에서 한 번만 정한다. 링이 프레임 예산을 다 쓰면 그 자리에서 멈추고 false를
    // 답한다: 예산은 공용 정책이 정하므로 D3D11도 같은 지점에서 멈춘다.
    Implementation& data = *mImplementation;
    if (transforms.empty())
    {
        return true;
    }
    if (!data.HasCommandList())
    {
        return false;
    }
    if (!Accepts(texture))
    {
        Diagnostics::Debug::LogError(
            "The D3D12 command list was handed a resolved texture from another backend.");
        return false;
    }
    if (pipelineKind != PipelineKind::Sprite && pipelineKind != PipelineKind::Text)
    {
        Diagnostics::Debug::LogError("D3D12 has no quad pipeline for this pipeline kind.");
        return false;
    }
    const auto& quadTexture = static_cast<const D3D12ResolvedTexture&>(texture);
    UINT textureDescriptorIndex = 0;
    if (!data.TryResolveTextureDescriptor(quadTexture, textureDescriptorIndex))
    {
        return false;
    }
    data.BindPipeline(pipelineKind);

    bool recorded = true;
    for (const QuadTransform& transform : transforms)
    {
        if (pipelineKind == PipelineKind::Text)
        {
            TextConstants constants = {};
            StoreTransposed(constants.worldViewProjection, transform.worldViewProjection);
            constants.tint = {
                transform.tint.r, transform.tint.g, transform.tint.b, transform.tint.a };
            constants.uvTransform = {
                transform.uvTransform[0], transform.uvTransform[1],
                transform.uvTransform[2], transform.uvTransform[3] };
            if (!data.frameResources->BindDrawConstants(
                    *data.commandList, constants, textureDescriptorIndex))
            {
                // 링이 다 찼다. 앞처럼 남은 quad도 계속 시도하고 — 그것들도 같은 자리에서
                // 멈춘다 — 배치 전체로는 false를 답한다.
                recorded = false;
                continue;
            }
        }
        else
        {
            SpriteConstants constants = {};
            StoreTransposed(constants.worldViewProjection, transform.worldViewProjection);
            constants.tint = {
                transform.tint.r, transform.tint.g, transform.tint.b, transform.tint.a };
            constants.uvTransform = {
                transform.uvTransform[0], transform.uvTransform[1],
                transform.uvTransform[2], transform.uvTransform[3] };
            if (!data.frameResources->BindDrawConstants(
                    *data.commandList, constants, textureDescriptorIndex))
            {
                recorded = false;
                continue;
            }
        }
        data.commandList->DrawInstanced(SpriteQuadVertexCount, 1, 0, 0);
    }
    return recorded;
}

void D3D12CommandList::EndPass(RenderPass)
{
    // D3D12 carries pipeline state on the pipeline state object rather than on the context, so a
    // pass leaves nothing bound that the next one has to undo.
}

}
