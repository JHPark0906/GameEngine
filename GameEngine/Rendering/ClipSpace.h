#pragma once

#include "../Math/Matrix.h"

namespace GameEngine::Rendering
{

/// <summary>투영을 적용한 뒤 클립 공간의 +Y가 어느 쪽을 향하는지이다.</summary>
enum class ClipSpaceYAxis : unsigned char
{
    Up,
    Down,
};

/// <summary>클립 공간이 시야 절두체를 사상하는 깊이 범위이다.</summary>
enum class ClipSpaceDepthRange : unsigned char
{
    ZeroToOne,
    MinusOneToOne,
};

/// <summary>그래픽 API가 정점 셰이더 출력 좌표에 기대하는 규약이다.</summary>
struct ClipSpaceConvention
{
    ClipSpaceYAxis yAxis = ClipSpaceYAxis::Up;
    ClipSpaceDepthRange depthRange = ClipSpaceDepthRange::ZeroToOne;

    [[nodiscard]] bool operator==(const ClipSpaceConvention&) const = default;
};

/// <summary>
/// RenderFrame의 모든 투영이 표현되는 클립 공간이다: 왼손 좌표계, +Y 위쪽, 깊이 [0, 1].
///
/// Runtime과 프론트엔드는 언제나 이 공간의 행렬을 만들므로, 장면은 어디서 그려지든 같아
/// 보인다. 산문으로만 설명하지 않고 여기 선언하는 이유는, API가 이에 동의하지 않는 백엔드가
/// 코드로 그렇게 말하고 보정받을 수 있어야 하기 때문이다.
/// </summary>
inline constexpr ClipSpaceConvention EngineClipSpace{
    ClipSpaceYAxis::Up, ClipSpaceDepthRange::ZeroToOne };

/// <summary>
/// 위치를 엔진의 클립 공간에서 대상의 클립 공간으로 옮기는 행렬이다. 투영 뒤에 적용한다. 대상이
/// 이미 엔진과 일치하면 단위 행렬을 반환한다.
///
/// Direct3D와 Metal은 엔진과 일치하므로 단위 행렬이다. Vulkan은 +Y가 아래고 OpenGL은 깊이를
/// [-1, 1]로 사상한다. 그런 API의 백엔드는 자기 투영을 고치는 대신 그렇게 선언하고 여기서
/// 보정받으므로, 보정은 한 번만 존재하고 장치 없이도 테스트할 수 있다.
/// </summary>
[[nodiscard]] Math::Matrix4x4 MakeClipSpaceCorrection(ClipSpaceConvention target);

/// <summary>
/// 완성된 world-view-projection 행렬에 대상 클립 공간의 보정을 적용한다. 대상이 엔진과 일치하면
/// 아무것도 하지 않으므로, 보정이 필요 없는 백엔드는 draw마다 행렬 곱을 치르지 않는다.
/// </summary>
void ApplyClipSpaceCorrection(
    Math::Matrix4x4& worldViewProjection, ClipSpaceConvention target);

}
