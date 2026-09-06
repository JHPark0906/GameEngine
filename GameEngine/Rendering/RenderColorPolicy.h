#pragma once

#include <cmath>

#include "../Math/Color.h"
#include "RenderFrame.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 색 공간 정책이다. 렌더링 전체에 하나뿐이고, 백엔드는 결정하지 않는다.
///
/// 장면에 저작된 색 — 틴트, 지우기 색, 광원 색 — 과 이미지 파일의 픽셀은 sRGB다. 사람이 고르고
/// 사람이 그린 값이 그것이기 때문이다. GPU의 산술 — 조명, 블렌딩, 필터링 — 은 선형 공간에서만
/// 맞는다. 그래서 경계는 이렇다: 텍스처는 sRGB 포맷으로 올려 샘플러가 선형으로 풀고, 상수로 가는
/// 색은 여기서 선형으로 풀고, 렌더 타깃은 sRGB 뷰로 두어 쓰는 순간 다시 인코딩된다. 캡처된
/// 이미지의 바이트는 그래서 sRGB이고, 그것을 다시 텍스처로 올릴 때도 sRGB 포맷이 맞다.
///
/// 이 규칙이 한 곳에 있는 이유는 두 백엔드가 각자 다르게 틀릴 여지를 없애기 위해서다: 텍스처를
/// UNORM으로 샘플링하고 틴트를 그대로 곱하면 블렌딩 결과가 물리적으로 틀린다.
/// </summary>

/// <summary>sRGB 인코딩된 채널 하나를 선형으로 푼다. IEC 61966-2-1의 정의 그대로다.</summary>
[[nodiscard]] inline float SrgbChannelToLinear(const float value)
{
    if (!(value > 0.0f))
    {
        return 0.0f;
    }
    if (value >= 1.0f)
    {
        return 1.0f;
    }
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

/// <summary>선형 채널 하나를 sRGB로 인코딩한다. 렌더 타깃 뷰가 하는 일을 CPU에서 되짚을 때 쓴다.</summary>
[[nodiscard]] inline float LinearChannelToSrgb(const float value)
{
    if (!(value > 0.0f))
    {
        return 0.0f;
    }
    if (value >= 1.0f)
    {
        return 1.0f;
    }
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

/// <summary>저작된 sRGB 색을 셰이더 상수로 보낼 선형 색으로 바꾼다. 알파는 그대로다.</summary>
[[nodiscard]] inline Math::Color ToLinearColor(const Math::Color& color)
{
    return { SrgbChannelToLinear(color.r), SrgbChannelToLinear(color.g), SrgbChannelToLinear(color.b), color.a };
}

/// <summary>
/// 프레임의 렌더 타깃을 지울 선형 색이다. 카메라가 없는 프레임은 검정이다. 백엔드는 이것을 그대로
/// ClearRenderTargetView에 넘긴다: 뷰가 sRGB라 지우기 값도 선형이어야 한다.
/// </summary>
[[nodiscard]] inline Math::Color GetLinearClearColor(const RenderFrame& frame)
{
    return ToLinearColor(frame.GetCamera() ? frame.GetCamera()->clearColor : Math::Color::Black);
}

}
