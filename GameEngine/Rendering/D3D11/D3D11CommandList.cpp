#include "pch.h"
#include "D3D11CommandList.h"


#include <windows.h>
#include <cstddef>
#include <cstring>
#include <span>
#include <iterator>
#include <memory>
#include <optional>
#include <wrl/client.h>

#include "D3D11ResolvedResources.h"
#include "ID3D11GraphicsDevice.h"
#include "../../Platform/Win32/Win32Diagnostics.h"
#include "../Direct3D/MatrixConversion.h"
#include "../Direct3D/ShaderCompiler.h"
#include "../MeshDrawGeometry.h"
#include "../ShaderInterop.h"
#include "../Direct3D/ShaderPaths.h"
#include "../RenderSamplerPolicy.h"
#include "../ShaderBindings.h"

namespace GameEngine::Rendering::D3D11
{

using Platform::Win32::LogHResult;
using Direct3D::StoreTransposed;

namespace
{
    /// <summary>공용 샘플러 정책을 이 API의 주소 모드로 번역한다.</summary>
    [[nodiscard]] D3D11_TEXTURE_ADDRESS_MODE ToAddressMode(const SamplerAddressMode mode)
    {
        return mode == SamplerAddressMode::Wrap
            ? D3D11_TEXTURE_ADDRESS_WRAP
            : D3D11_TEXTURE_ADDRESS_CLAMP;
    }

    /// <summary>공용 정책이 파이프라인 종류에 처방한 샘플러를 만든다.</summary>
    [[nodiscard]] bool CreateSampler(
        ID3D11Device& device,
        const PipelineKind kind,
        const char* const name,
        Microsoft::WRL::ComPtr<ID3D11SamplerState>& sampler)
    {
        const D3D11_TEXTURE_ADDRESS_MODE addressMode =
            ToAddressMode(GetSamplerAddressMode(kind));
        D3D11_SAMPLER_DESC descriptor = {};
        descriptor.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        descriptor.AddressU = addressMode;
        descriptor.AddressV = addressMode;
        descriptor.AddressW = addressMode;
        descriptor.MaxLOD = D3D11_FLOAT32_MAX;
        const HRESULT result = device.CreateSamplerState(&descriptor, sampler.ReleaseAndGetAddressOf());
        if (FAILED(result))
        {
            LogHResult(name, result);
            return false;
        }
        return true;
    }

}

struct D3D11CommandList::Implementation
{
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;

    /// <summary>
    /// 컨텍스트가 현재 쥔 파이프라인이다. 같은 파이프라인을 공유하는 연속된 draw는 한 번만
    /// 바인딩한다. 프레임은 파이프라인별로 정렬되어 도착하므로 연속 draw는 대개 일치한다.
    /// draw마다 바인딩하면 이미 설정된 상태를 다시 설정하는 컨텍스트 호출이 열한 번씩 든다.
    /// </summary>
    std::optional<PipelineKind> boundPipeline;
    bool boundAlphaBlended = false;

    // Mesh pipeline.
    Microsoft::WRL::ComPtr<ID3D11InputLayout> meshInputLayout;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> meshVertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> meshPixelShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> meshConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> meshSampler;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> meshRasterizer;

    // Skinned mesh pipeline. 샘플러·래스터라이저·불투명 블렌드는 정적 메시와 그릴 방식이
    // 같으므로 위 것을 그대로 다시 쓴다 — 달라지는 것은 정점 셰이더와 정점 레이아웃, 그리고
    // 뼈 행렬을 위한 두 번째 상수 버퍼뿐이다.
    Microsoft::WRL::ComPtr<ID3D11InputLayout> skinnedMeshInputLayout;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> skinnedMeshVertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> skinnedMeshPixelShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> skinnedMeshConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> boneConstantBuffer;

    // Quad pipelines. The sprite and text shaders declare the same VS_INPUT, so they share one
    // input layout, one quad vertex buffer, and one set of blend, rasterizer, and depth states.
    Microsoft::WRL::ComPtr<ID3D11InputLayout> quadInputLayout;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> spriteVertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> spritePixelShader;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> textVertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> textPixelShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> quadVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> spriteConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> textConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> quadSampler;
    Microsoft::WRL::ComPtr<ID3D11BlendState> quadBlendState;
    Microsoft::WRL::ComPtr<ID3D11BlendState> opaqueBlendState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> quadRasterizer;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> quadDepthStencilState;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> blendedMeshDepthStencilState;

    void BindMeshPipeline(const bool alphaBlended)
    {
        if (boundPipeline == PipelineKind::Mesh && boundAlphaBlended == alphaBlended)
        {
            return;
        }
        boundPipeline = PipelineKind::Mesh;
        boundAlphaBlended = alphaBlended;

        ID3D11Buffer* constants = meshConstantBuffer.Get();
        ID3D11SamplerState* sampler = meshSampler.Get();
        constexpr float blendFactor[4] = {};
        constexpr ShaderBindingSlots slots = GetShaderBindings(PipelineKind::Mesh);

        context->IASetInputLayout(meshInputLayout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(meshVertexShader.Get(), nullptr, 0);
        context->VSSetConstantBuffers(slots.constantBuffer, 1, &constants);
        context->PSSetShader(meshPixelShader.Get(), nullptr, 0);
        context->PSSetConstantBuffers(slots.constantBuffer, 1, &constants);
        context->PSSetSamplers(slots.sampler, 1, &sampler);
        context->RSSetState(meshRasterizer.Get());
        // Bound explicitly rather than inherited: an opaque mesh must not pick up whatever blend
        // state the previous pass happened to leave behind.
        context->OMSetBlendState(alphaBlended ? quadBlendState.Get() : opaqueBlendState.Get(),
            blendFactor, 0xffffffff);
        context->OMSetDepthStencilState(alphaBlended ? blendedMeshDepthStencilState.Get() : nullptr, 0);
    }

    void BindSkinnedMeshPipeline(const bool alphaBlended)
    {
        if (boundPipeline == PipelineKind::SkinnedMesh && boundAlphaBlended == alphaBlended)
        {
            return;
        }
        boundPipeline = PipelineKind::SkinnedMesh;
        boundAlphaBlended = alphaBlended;

        ID3D11Buffer* constants = skinnedMeshConstantBuffer.Get();
        ID3D11Buffer* boneConstants = boneConstantBuffer.Get();
        ID3D11SamplerState* sampler = meshSampler.Get();
        constexpr float blendFactor[4] = {};
        constexpr ShaderBindingSlots slots = GetShaderBindings(PipelineKind::SkinnedMesh);

        context->IASetInputLayout(skinnedMeshInputLayout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(skinnedMeshVertexShader.Get(), nullptr, 0);
        context->VSSetConstantBuffers(slots.constantBuffer, 1, &constants);
        context->VSSetConstantBuffers(SkinnedMeshBoneConstantBufferSlot, 1, &boneConstants);
        context->PSSetShader(skinnedMeshPixelShader.Get(), nullptr, 0);
        context->PSSetConstantBuffers(slots.constantBuffer, 1, &constants);
        context->PSSetSamplers(slots.sampler, 1, &sampler);
        context->RSSetState(meshRasterizer.Get());
        context->OMSetBlendState(alphaBlended ? quadBlendState.Get() : opaqueBlendState.Get(),
            blendFactor, 0xffffffff);
        context->OMSetDepthStencilState(alphaBlended ? blendedMeshDepthStencilState.Get() : nullptr, 0);
    }

    void BindQuadPipeline(const PipelineKind kind)
    {
        if (boundPipeline == kind)
        {
            return;
        }
        boundPipeline = kind;

        const bool isText = kind == PipelineKind::Text;
        ID3D11VertexShader* const vertexShader =
            isText ? textVertexShader.Get() : spriteVertexShader.Get();
        ID3D11PixelShader* const pixelShader =
            isText ? textPixelShader.Get() : spritePixelShader.Get();
        ID3D11Buffer* constantBuffer =
            isText ? textConstantBuffer.Get() : spriteConstantBuffer.Get();

        constexpr UINT stride = sizeof(SpriteVertex);
        constexpr UINT offset = 0;
        ID3D11Buffer* vertexBuffer = quadVertexBuffer.Get();
        ID3D11SamplerState* sampler = quadSampler.Get();
        constexpr float blendFactor[4] = {};
        const ShaderBindingSlots slots = GetShaderBindings(kind);

        context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        context->IASetInputLayout(quadInputLayout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertexShader, nullptr, 0);
        context->VSSetConstantBuffers(slots.constantBuffer, 1, &constantBuffer);
        context->PSSetShader(pixelShader, nullptr, 0);
        context->PSSetConstantBuffers(slots.constantBuffer, 1, &constantBuffer);
        context->PSSetSamplers(slots.sampler, 1, &sampler);
        context->RSSetState(quadRasterizer.Get());
        context->OMSetDepthStencilState(quadDepthStencilState.Get(), 0);
        context->OMSetBlendState(quadBlendState.Get(), blendFactor, 0xffffffff);
    }

    /// <summary>
    /// 배치가 함께 보는 텍스처를 픽셀 셰이더에 건다. 배치의 quad마다 다시 걸 것이 아니라 배치가
    /// 시작할 때 한 번 건다 — <c>DrawQuads</c>의 텍스처는 배치 전체를 위한 인자 하나다.
    /// </summary>
    void BindQuadTexture(const PipelineKind kind, ID3D11ShaderResourceView* const textureView) const
    {
        const unsigned int textureSlot = GetShaderBindings(kind).texture;
        context->PSSetShaderResources(textureSlot, 1, &textureView);
    }

    /// <summary>
    /// 걸어 둔 텍스처를 내린다. 배치 <b>끝에</b> 한 번 부른다.
    ///
    /// 이 되돌리기는 보통 「샘플링하던 텍스처가 곧 렌더 타깃이 될 때의 읽기/쓰기 충돌」을 막는
    /// 관용구인데, 이 백엔드에서는 그 충돌이 생길 수 없다: 에셋 텍스처는 언제나
    /// <c>D3D11_BIND_SHADER_RESOURCE</c> 하나로만 만들어지고, 캡처 대상은
    /// <c>D3D11_BIND_RENDER_TARGET</c>만 가진 별개의 텍스처이며 셰이더 리소스 뷰가 아예 없다.
    /// 그래서 남겨 두는 이유는 위험이 아니라 위생이다 — 다음 패스가 남의 텍스처를 물려받은 채
    /// 시작하지 않게.
    /// </summary>
    void UnbindQuadTexture(const PipelineKind kind) const
    {
        ID3D11ShaderResourceView* nullView = nullptr;
        BindQuadTexture(kind, nullView);
    }

    /// <summary>
    /// 한 draw의 상수를 업로드하고 공용 quad draw를 기록한다. 텍스처는 이미 걸려 있다고 본다:
    /// D3D11은 draw마다 상수를 새로 매핑해야 하지만 텍스처는 그럴 이유가 없다.
    /// </summary>
    template <typename TConstants>
    [[nodiscard]] bool DrawQuadWithConstants(
        ID3D11Buffer* const constantBuffer, const TConstants& constants) const
    {
        D3D11_MAPPED_SUBRESOURCE mappedConstants = {};
        const HRESULT mapResult = context->Map(
            constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedConstants);
        if (FAILED(mapResult))
        {
            LogHResult("Mapping the D3D11 quad constant buffer", mapResult);
            return false;
        }
        std::memcpy(mappedConstants.pData, &constants, sizeof(constants));
        context->Unmap(constantBuffer, 0);

        context->Draw(static_cast<UINT>(SpriteQuadVertices.size()), 0);
        return true;
    }

    [[nodiscard]] bool InitializeMeshPipeline(ID3D11Device& device);
    [[nodiscard]] bool InitializeSkinnedMeshPipeline(ID3D11Device& device);
    [[nodiscard]] bool InitializeQuadPipelines(ID3D11Device& device);
};

bool D3D11CommandList::Implementation::InitializeMeshPipeline(ID3D11Device& device)
{
    Microsoft::WRL::ComPtr<ID3DBlob> vertexBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelBlob;
    if (!Direct3D::CompileShader(ShaderProgram::Mesh, "VS", "vs_5_0", vertexBlob) ||
        !Direct3D::CompileShader(ShaderProgram::Mesh, "PS", "ps_5_0", pixelBlob))
    {
        return false;
    }

    HRESULT result = device.CreateVertexShader(
        vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr,
        meshVertexShader.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 mesh vertex shader", result);
        return false;
    }
    result = device.CreatePixelShader(
        pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr,
        meshPixelShader.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 mesh pixel shader", result);
        return false;
    }

    constexpr D3D11_INPUT_ELEMENT_DESC inputElements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshVertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshVertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(MeshVertex, textureCoordinate), D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    result = device.CreateInputLayout(
        inputElements, static_cast<UINT>(std::size(inputElements)),
        vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(),
        meshInputLayout.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 mesh input layout", result);
        return false;
    }

    D3D11_BUFFER_DESC bufferDescriptor = {};
    bufferDescriptor.ByteWidth = sizeof(MeshConstants);
    bufferDescriptor.Usage = D3D11_USAGE_DEFAULT;
    bufferDescriptor.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    result = device.CreateBuffer(
        &bufferDescriptor, nullptr, meshConstantBuffer.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 mesh constant buffer", result);
        return false;
    }

    if (!CreateSampler(
            device, PipelineKind::Mesh, "Failed to create the D3D11 mesh sampler", meshSampler))
    {
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDescriptor = {};
    rasterizerDescriptor.FillMode = D3D11_FILL_SOLID;
    rasterizerDescriptor.CullMode = D3D11_CULL_BACK;
    rasterizerDescriptor.FrontCounterClockwise = false;
    rasterizerDescriptor.DepthClipEnable = true;
    result = device.CreateRasterizerState(
        &rasterizerDescriptor, meshRasterizer.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 mesh rasterizer state", result);
        return false;
    }

    D3D11_BLEND_DESC opaqueBlendDescriptor = {};
    opaqueBlendDescriptor.RenderTarget[0].BlendEnable = false;
    opaqueBlendDescriptor.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    result = device.CreateBlendState(
        &opaqueBlendDescriptor, opaqueBlendState.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 opaque blend state", result);
        return false;
    }
    D3D11_DEPTH_STENCIL_DESC blendedDepthDescriptor = {};
    blendedDepthDescriptor.DepthEnable = TRUE;
    blendedDepthDescriptor.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    blendedDepthDescriptor.DepthFunc = D3D11_COMPARISON_LESS;
    result = device.CreateDepthStencilState(
        &blendedDepthDescriptor, blendedMeshDepthStencilState.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 blended mesh depth state", result);
        return false;
    }
    return true;
}

bool D3D11CommandList::Implementation::InitializeSkinnedMeshPipeline(ID3D11Device& device)
{
    Microsoft::WRL::ComPtr<ID3DBlob> vertexBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelBlob;
    if (!Direct3D::CompileShader(ShaderProgram::SkinnedMesh, "VS", "vs_5_0", vertexBlob) ||
        !Direct3D::CompileShader(ShaderProgram::SkinnedMesh, "PS", "ps_5_0", pixelBlob))
    {
        return false;
    }

    HRESULT result = device.CreateVertexShader(
        vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr,
        skinnedMeshVertexShader.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 skinned mesh vertex shader", result);
        return false;
    }
    result = device.CreatePixelShader(
        pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr,
        skinnedMeshPixelShader.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 skinned mesh pixel shader", result);
        return false;
    }

    constexpr D3D11_INPUT_ELEMENT_DESC inputElements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SkinnedMeshVertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SkinnedMeshVertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SkinnedMeshVertex, textureCoordinate), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, offsetof(SkinnedMeshVertex, boneIndices), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(SkinnedMeshVertex, boneWeights), D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    result = device.CreateInputLayout(
        inputElements, static_cast<UINT>(std::size(inputElements)),
        vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(),
        skinnedMeshInputLayout.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 skinned mesh input layout", result);
        return false;
    }

    D3D11_BUFFER_DESC bufferDescriptor = {};
    bufferDescriptor.ByteWidth = sizeof(MeshConstants);
    bufferDescriptor.Usage = D3D11_USAGE_DEFAULT;
    bufferDescriptor.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    result = device.CreateBuffer(
        &bufferDescriptor, nullptr, skinnedMeshConstantBuffer.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 skinned mesh constant buffer", result);
        return false;
    }

    bufferDescriptor.ByteWidth = sizeof(BoneConstants);
    result = device.CreateBuffer(
        &bufferDescriptor, nullptr, boneConstantBuffer.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 bone constant buffer", result);
        return false;
    }
    return true;
}

bool D3D11CommandList::Implementation::InitializeQuadPipelines(ID3D11Device& device)
{
    Microsoft::WRL::ComPtr<ID3DBlob> spriteVertexBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> spritePixelBlob;
    if (!Direct3D::CompileShader(ShaderProgram::Sprite, "VS", "vs_5_0", spriteVertexBlob) ||
        !Direct3D::CompileShader(ShaderProgram::Sprite, "PS", "ps_5_0", spritePixelBlob))
    {
        return false;
    }
    HRESULT result = device.CreateVertexShader(
        spriteVertexBlob->GetBufferPointer(), spriteVertexBlob->GetBufferSize(), nullptr,
        spriteVertexShader.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Creating the D3D11 sprite vertex shader", result);
        return false;
    }
    result = device.CreatePixelShader(
        spritePixelBlob->GetBufferPointer(), spritePixelBlob->GetBufferSize(), nullptr,
        spritePixelShader.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Creating the D3D11 sprite pixel shader", result);
        return false;
    }

    // Text has its own pipeline so PipelineKind::Text means the same thing on both backends: the
    // dedicated text shader sampling single-channel coverage.
    Microsoft::WRL::ComPtr<ID3DBlob> textVertexBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> textPixelBlob;
    if (!Direct3D::CompileShader(ShaderProgram::Text, "VS", "vs_5_0", textVertexBlob) ||
        !Direct3D::CompileShader(ShaderProgram::Text, "PS", "ps_5_0", textPixelBlob))
    {
        return false;
    }
    result = device.CreateVertexShader(
        textVertexBlob->GetBufferPointer(), textVertexBlob->GetBufferSize(), nullptr,
        textVertexShader.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Creating the D3D11 text vertex shader", result);
        return false;
    }
    result = device.CreatePixelShader(
        textPixelBlob->GetBufferPointer(), textPixelBlob->GetBufferSize(), nullptr,
        textPixelShader.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Creating the D3D11 text pixel shader", result);
        return false;
    }

    constexpr D3D11_INPUT_ELEMENT_DESC inputElements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SpriteVertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SpriteVertex, textureCoordinate), D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    result = device.CreateInputLayout(
        inputElements, static_cast<UINT>(std::size(inputElements)),
        spriteVertexBlob->GetBufferPointer(), spriteVertexBlob->GetBufferSize(),
        quadInputLayout.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Creating the D3D11 quad input layout", result);
        return false;
    }

    D3D11_BUFFER_DESC bufferDescriptor = {};
    bufferDescriptor.ByteWidth = static_cast<UINT>(sizeof(SpriteQuadVertices));
    bufferDescriptor.Usage = D3D11_USAGE_IMMUTABLE;
    bufferDescriptor.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    const D3D11_SUBRESOURCE_DATA initialData = { SpriteQuadVertices.data() };
    result = device.CreateBuffer(
        &bufferDescriptor, &initialData, quadVertexBuffer.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 sprite vertex buffer", result);
        return false;
    }

    bufferDescriptor = {};
    bufferDescriptor.ByteWidth = sizeof(SpriteConstants);
    bufferDescriptor.Usage = D3D11_USAGE_DYNAMIC;
    bufferDescriptor.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDescriptor.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    result = device.CreateBuffer(
        &bufferDescriptor, nullptr, spriteConstantBuffer.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Creating the D3D11 sprite constant buffer", result);
        return false;
    }

    bufferDescriptor.ByteWidth = sizeof(TextConstants);
    result = device.CreateBuffer(
        &bufferDescriptor, nullptr, textConstantBuffer.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Creating the D3D11 text constant buffer", result);
        return false;
    }

    if (!CreateSampler(
            device, PipelineKind::Sprite, "Failed to create the D3D11 quad sampler", quadSampler))
    {
        return false;
    }

    D3D11_BLEND_DESC blendDescriptor = {};
    blendDescriptor.RenderTarget[0].BlendEnable = true;
    blendDescriptor.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDescriptor.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDescriptor.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDescriptor.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDescriptor.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDescriptor.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDescriptor.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    result = device.CreateBlendState(
        &blendDescriptor, quadBlendState.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 sprite blend state", result);
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDescriptor = {};
    rasterizerDescriptor.FillMode = D3D11_FILL_SOLID;
    rasterizerDescriptor.CullMode = D3D11_CULL_NONE;
    rasterizerDescriptor.DepthClipEnable = true;
    result = device.CreateRasterizerState(
        &rasterizerDescriptor, quadRasterizer.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 sprite rasterizer state", result);
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depthStencilDescriptor = {};
    depthStencilDescriptor.DepthEnable = false;
    depthStencilDescriptor.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    result = device.CreateDepthStencilState(
        &depthStencilDescriptor, quadDepthStencilState.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D11 sprite depth-stencil state", result);
        return false;
    }
    return true;
}

D3D11CommandList::D3D11CommandList()
    : mImplementation(std::make_unique<Implementation>())
{
}

D3D11CommandList::~D3D11CommandList() = default;

bool D3D11CommandList::Initialize(ID3D11GraphicsDevice& graphicsDevice)
{
    ID3D11Device* const device = graphicsDevice.GetDevice();
    if (!device)
    {
        Diagnostics::Debug::LogError("A valid D3D11 device is required by the command list.");
        return false;
    }
    mImplementation->context = graphicsDevice.GetDeviceContext();
    if (!mImplementation->context)
    {
        Diagnostics::Debug::LogError("A valid D3D11 device context is required by the command list.");
        return false;
    }
    return mImplementation->InitializeMeshPipeline(*device) &&
        mImplementation->InitializeSkinnedMeshPipeline(*device) &&
        mImplementation->InitializeQuadPipelines(*device);
}

void D3D11CommandList::BeginFrame()
{
    mImplementation->boundPipeline.reset();
}

const char* D3D11CommandList::GetBackendName() const
{
    return "D3D11";
}

bool D3D11CommandList::DrawMesh(
    const MeshShading& transform,
    const IResolvedGeometry& geometry,
    const IResolvedTexture& material)
{
    if (!Accepts(geometry) || !Accepts(material))
    {
        Diagnostics::Debug::LogError(
            "The D3D11 command list was handed a resolved resource from another backend.");
        return false;
    }
    const auto& meshGeometry = static_cast<const D3D11ResolvedGeometry&>(geometry);
    const auto& meshMaterial = static_cast<const D3D11ResolvedTexture&>(material);

    Implementation& data = *mImplementation;
    data.BindMeshPipeline(transform.alphaBlended);

    MeshConstants constants;
    StoreTransposed(constants.worldViewProjection, transform.worldViewProjection);
    StoreTransposed(constants.world, transform.world);
    StoreTransposed(constants.normalToWorld, transform.normalToWorld);
    StoreMeshLighting(transform, constants);
    data.context->UpdateSubresource(
        data.meshConstantBuffer.Get(), 0, nullptr, &constants, 0, 0);

    constexpr UINT stride = sizeof(MeshVertex);
    constexpr UINT offset = 0;
    ID3D11Buffer* vertexBuffer = meshGeometry.vertexBuffer.Get();
    data.context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    data.context->IASetIndexBuffer(meshGeometry.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    constexpr unsigned int textureSlot = GetShaderBindings(PipelineKind::Mesh).texture;
    ID3D11ShaderResourceView* textureView = meshMaterial.shaderResourceView.Get();
    data.context->PSSetShaderResources(textureSlot, 1, &textureView);
    data.context->DrawIndexed(meshGeometry.indexCount, 0, 0);

    ID3D11ShaderResourceView* nullView = nullptr;
    data.context->PSSetShaderResources(textureSlot, 1, &nullView);
    return true;
}

bool D3D11CommandList::DrawSkinnedMesh(
    const MeshShading& transform,
    const IResolvedGeometry& geometry,
    const IResolvedTexture& material,
    const std::span<const Math::Matrix4x4> boneMatrices)
{
    if (!Accepts(geometry) || !Accepts(material))
    {
        Diagnostics::Debug::LogError(
            "The D3D11 command list was handed a resolved resource from another backend.");
        return false;
    }
    if (boneMatrices.size() > MaxSkinnedMeshBones)
    {
        Diagnostics::Debug::LogError(
            "A skinned mesh draw named more bones than the shader can hold. count=",
            boneMatrices.size(), ", max=", MaxSkinnedMeshBones);
        return false;
    }
    const auto& skinnedGeometry = static_cast<const D3D11ResolvedSkinnedGeometry&>(geometry);
    const auto& meshMaterial = static_cast<const D3D11ResolvedTexture&>(material);

    Implementation& data = *mImplementation;
    data.BindSkinnedMeshPipeline(transform.alphaBlended);

    MeshConstants constants;
    StoreTransposed(constants.worldViewProjection, transform.worldViewProjection);
    StoreTransposed(constants.world, transform.world);
    StoreTransposed(constants.normalToWorld, transform.normalToWorld);
    StoreMeshLighting(transform, constants);
    data.context->UpdateSubresource(
        data.skinnedMeshConstantBuffer.Get(), 0, nullptr, &constants, 0, 0);

    // 안 쓰는 뼈 칸은 손대지 않는다: 정점의 boneIndices는 언제나 임포트된 골격의 실제 뼈만
    // 가리키므로, 넘긴 것보다 뒤에 있는 칸을 읽는 정점이 없다.
    BoneConstants boneConstants;
    for (std::size_t index = 0; index < boneMatrices.size(); ++index)
    {
        StoreTransposed(boneConstants.boneMatrices[index], boneMatrices[index]);
    }
    data.context->UpdateSubresource(
        data.boneConstantBuffer.Get(), 0, nullptr, &boneConstants, 0, 0);

    constexpr UINT stride = sizeof(SkinnedMeshVertex);
    constexpr UINT offset = 0;
    ID3D11Buffer* vertexBuffer = skinnedGeometry.vertexBuffer.Get();
    data.context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    data.context->IASetIndexBuffer(skinnedGeometry.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    constexpr unsigned int textureSlot = GetShaderBindings(PipelineKind::SkinnedMesh).texture;
    ID3D11ShaderResourceView* textureView = meshMaterial.shaderResourceView.Get();
    data.context->PSSetShaderResources(textureSlot, 1, &textureView);
    data.context->DrawIndexed(skinnedGeometry.indexCount, 0, 0);

    ID3D11ShaderResourceView* nullView = nullptr;
    data.context->PSSetShaderResources(textureSlot, 1, &nullView);
    return true;
}

bool D3D11CommandList::DrawQuad(
    const PipelineKind pipelineKind,
    const QuadTransform& transform,
    const IResolvedTexture& texture)
{
    // quad 하나는 크기 1짜리 배치로 제출해 단일 draw와 배치가 같은 경로를 사용한다.
    return DrawQuads(pipelineKind, std::span{ &transform, 1 }, texture);
}


bool D3D11CommandList::DrawQuads(
    const PipelineKind pipelineKind,
    const std::span<const QuadTransform> transforms,
    const IResolvedTexture& texture)
{
    // D3D11은 draw마다 WRITE_DISCARD로 상수를 새로 매핑하므로 그 부분은 quad마다 남는다. 배치
    // 전체가 함께 보는 것 — 어느 백엔드의 텍스처인지, 파이프라인, 그리고 텍스처 자체 — 은 고리
    // 밖에서 한 번만 한다. 배치라는 사실이 인터페이스에 남아 있으므로, 이 백엔드가 나중에
    // 인스턴싱으로 옮겨 가도 공용 패스는 바뀌지 않는다.
    if (transforms.empty())
    {
        return true;
    }
    if (!Accepts(texture))
    {
        Diagnostics::Debug::LogError(
            "The D3D11 command list was handed a resolved texture from another backend.");
        return false;
    }
    if (pipelineKind != PipelineKind::Sprite && pipelineKind != PipelineKind::Text)
    {
        Diagnostics::Debug::LogError("D3D11 has no quad pipeline for this pipeline kind.");
        return false;
    }
    const auto& quadTexture = static_cast<const D3D11ResolvedTexture&>(texture);
    Implementation& data = *mImplementation;
    data.BindQuadPipeline(pipelineKind);
    data.BindQuadTexture(pipelineKind, quadTexture.shaderResourceView.Get());

    ID3D11Buffer* const constantBuffer = pipelineKind == PipelineKind::Text
        ? data.textConstantBuffer.Get()
        : data.spriteConstantBuffer.Get();

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
            recorded = data.DrawQuadWithConstants(constantBuffer, constants) && recorded;
            continue;
        }

        SpriteConstants constants = {};
        StoreTransposed(constants.worldViewProjection, transform.worldViewProjection);
        constants.tint = { transform.tint.r, transform.tint.g, transform.tint.b, transform.tint.a };
        constants.uvTransform = {
            transform.uvTransform[0], transform.uvTransform[1],
            transform.uvTransform[2], transform.uvTransform[3] };
        recorded = data.DrawQuadWithConstants(constantBuffer, constants) && recorded;
    }

    data.UnbindQuadTexture(pipelineKind);
    return recorded;
}

void D3D11CommandList::EndPass(const RenderPass pass)
{
    if (pass == RenderPass::Opaque)
    {
        return;
    }

    // D3D11 state is sticky, so each blended pass — transparent and overlay — returns the context
    // to its defaults instead of leaving alpha blending and the disabled depth test bound for
    // whatever records next.
    ID3D11ShaderResourceView* nullView = nullptr;
    constexpr float blendFactor[4] = {};
    constexpr unsigned int textureSlot = GetShaderBindings(PipelineKind::Sprite).texture;
    mImplementation->context->PSSetShaderResources(textureSlot, 1, &nullView);
    mImplementation->context->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
    mImplementation->context->OMSetDepthStencilState(nullptr, 0);
    mImplementation->boundPipeline.reset();
}

}
