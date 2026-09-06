#pragma once

#include <d3d12.h>

namespace GameEngine::Rendering::D3D12
{

/// <summary>
/// 모든 파이프라인 상태가 맞춰야 하는 렌더 타깃·깊이 포맷이다. D3D12GraphicsDevice가 swap
/// chain과 깊이 버퍼를 이 포맷으로 만들므로, 어긋난 파이프라인은 검증에 실패한다.
/// </summary>
/// <summary>
/// 파이프라인이 그리는 렌더 타깃 뷰의 포맷이다. sRGB라 셰이더는 선형 값을 쓰고 뷰가 인코딩한다.
/// 스왑 체인 버퍼 자체는 UNORM이다(flip 모델은 sRGB 버퍼를 거부한다). 뷰만 이 포맷이다.
/// </summary>
inline constexpr DXGI_FORMAT RenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
inline constexpr DXGI_FORMAT DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

/// <summary>모든 파이프라인이 공유하는 표준 source-alpha 블렌딩이다.</summary>
inline constexpr D3D12_RENDER_TARGET_BLEND_DESC AlphaBlendDescription =
{
    TRUE, FALSE,
    D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
    D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
    D3D12_LOGIC_OP_NOOP,
    D3D12_COLOR_WRITE_ENABLE_ALL
};

/// <summary>
/// 모든 D3D12 패스가 공유하는 파이프라인 상태이다: 알파 블렌딩, solid fill, 컬링 없음, 깊이
/// 테스트 꺼짐. 패스는 자기 단계가 실제로 바꾸는 것만 덮어쓰므로, 다르게 할 의도가 없던 설정에서
/// 패스들이 서로 어긋날 수 없다.
/// </summary>
[[nodiscard]] inline D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeGraphicsPipelineDescription(
    ID3D12RootSignature& rootSignature)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline = {};
    pipeline.pRootSignature = &rootSignature;
    pipeline.BlendState.AlphaToCoverageEnable = FALSE;
    pipeline.BlendState.IndependentBlendEnable = FALSE;
    for (D3D12_RENDER_TARGET_BLEND_DESC& target : pipeline.BlendState.RenderTarget)
    {
        target = AlphaBlendDescription;
    }
    pipeline.SampleMask = UINT_MAX;
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.DepthStencilState.DepthEnable = FALSE;
    pipeline.DepthStencilState.StencilEnable = FALSE;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = RenderTargetFormat;
    pipeline.DSVFormat = DepthStencilFormat;
    pipeline.SampleDesc.Count = 1;
    return pipeline;
}

}
