#pragma once

#include "RenderFrame.h"

namespace GameEngine::Rendering
{

/// <summary>샘플러가 [0, 1] 범위 밖의 텍스처 좌표를 다루는 방식이다.</summary>
enum class SamplerAddressMode : unsigned char
{
    /// <summary>좌표가 반복되어, 타일 텍스처가 가장자리를 지나 이어진다.</summary>
    Wrap,
    /// <summary>좌표가 가장자리 텍셀에서 멈추어, 필터링이 반대편에 닿을 수 없다.</summary>
    Clamp,
};

/// <summary>
/// 각 파이프라인 종류가 자기 텍스처를 샘플링하는 방식이다.
///
/// 그래픽 API의 속성이 아닌 렌더링 정책이므로 모든 백엔드가 이 규칙을 공유한다.
/// 스프라이트나 글리프 아틀라스를 wrap으로 샘플링하면 쌍선형 필터링이 반대편 가장자리
/// 텍셀을 끌어오므로 파이프라인에 맞는 주소 모드를 사용해야 한다.
///
/// 샘플러의 바인딩 슬롯은 셰이더 인터페이스이므로 ShaderBindings.h가 정한다.
/// </summary>
[[nodiscard]] constexpr SamplerAddressMode GetSamplerAddressMode(const PipelineKind kind)
{
    // A mesh material is expected to tile — a skinned mesh is still a mesh, and its material is
    // authored the same way. A sprite and a rasterized glyph run occupy their whole texture, so
    // anything sampled past the edge is bleed rather than content.
    return kind == PipelineKind::Mesh || kind == PipelineKind::SkinnedMesh
        ? SamplerAddressMode::Wrap
        : SamplerAddressMode::Clamp;
}

static_assert(
    GetSamplerAddressMode(PipelineKind::Sprite) == GetSamplerAddressMode(PipelineKind::Text),
    "A sprite and a line of text are both quads and must sample their texture the same way.");

}
