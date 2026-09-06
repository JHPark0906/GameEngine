#pragma once

#include "GlyphRasterizer.h"

namespace GameEngine::Text
{

/// <summary>
/// 그려진 글리프 하나를 구조로 재어 낸 값들이다.
///
/// 힌팅 여부에 따라 획의 가장자리 픽셀이 달라질 수 있으므로, 픽셀 일치 대신 잉크의 범위,
/// 양과 획의 굵기를 비교한다. 가장자리의 작은 차이에 덜 민감하면서 글자 구조의 차이를 나타낸다.
/// </summary>
struct GlyphStructure
{
    /// <summary>잉크가 있는 자리의 경계다. 글리프 배치 원점 기준의 픽셀이다.</summary>
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    /// <summary>커버리지의 총합이다. 잉크의 넓이를 픽셀로 잰 값이 된다.</summary>
    double inkArea = 0.0;

    /// <summary>
    /// 가로 획 두께의 중심값이다.
    ///
    /// 주사선마다 잉크가 이어지는 구간의 길이를 모아 그 중심값을 취한다. 평균이 아니라
    /// 중심값인 이유는, 글자마다 유난히 길거나 짧은 구간이 하나둘 섞이는데 평균은 그것에
    /// 끌려가기 때문이다.
    ///
    /// 이 값이 이 구조에서 가장 벼른 것이다: 힌팅은 획을 픽셀 격자에 맞추면서 <b>굵기를
    /// 바꾼다.</b> 그러니 우리와 플랫폼 렌더러의 획 두께를 견주면 「가장자리가 다르다」와
    /// 「획이 실제로 더 굵다」를 가를 수 있다.
    /// </summary>
    float strokeWidth = 0.0f;

    /// <summary>세로 획 두께의 중심값이다. 가로와 같은 방법을 열에 대해 쓴다.</summary>
    float strokeHeight = 0.0f;

    [[nodiscard]] float GetWidth() const { return right - left; }
    [[nodiscard]] float GetHeight() const { return bottom - top; }
    [[nodiscard]] bool HasInk() const { return right > left && bottom > top; }
};

/// <summary>
/// 커버리지 비트맵에서 구조를 잰다.
/// </summary>
/// <param name="alphaPixels">픽셀당 1바이트, 위에서 아래 순서다.</param>
/// <param name="width">비트맵의 가로 픽셀 수다.</param>
/// <param name="height">비트맵의 세로 픽셀 수다.</param>
/// <param name="originX">글리프 배치 원점에서 비트맵 좌상단까지의 가로 거리다.</param>
/// <param name="originY">같은 세로 거리다.</param>
/// <param name="inkThreshold">
/// 이 값보다 커야 잉크로 친다.
///
/// 낮게 잡는다. 절반(128)으로 자르면 <b>힌팅하지 않은 얇은 획이 통째로 사라진다</b> — 작은
/// 크기에서 우리 획은 커버리지가 100 언저리인 일이 흔하고, 힌팅한 렌더러의 같은 획은 격자에
/// 붙어 꽉 차 있다. 그 문턱으로 재면 「우리 글자가 4픽셀 짧다」 같은 값이 나오는데, 그것은
/// 글자의 성질이 아니라 자의 성질이다.
/// </param>
[[nodiscard]] GlyphStructure MeasureGlyphStructure(
    std::span<const std::byte> alphaPixels,
    unsigned int width,
    unsigned int height,
    float originX,
    float originY,
    unsigned char inkThreshold = 32);

/// <summary>우리 래스터라이저가 낸 비트맵을 잰다.</summary>
[[nodiscard]] GlyphStructure MeasureGlyphStructure(
    const CoverageBitmap& bitmap, unsigned char inkThreshold = 32);

}
