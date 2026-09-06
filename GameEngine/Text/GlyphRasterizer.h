#pragma once

#include <cstddef>
#include <vector>

#include "GlyphOutline.h"

namespace GameEngine::Text
{

/// <summary>
/// 글리프 하나의 커버리지 비트맵이다. 픽셀당 1바이트, 위에서 아래 순서다.
///
/// <see cref="originX"/>·<see cref="originY"/>는 글리프의 배치 원점 — 베이스라인 위의 펜 자리 —
/// 에서 비트맵 좌상단까지의 픽셀 거리다. 잉크가 없는 글리프는 크기가 0이며 그것도 성공이다.
/// </summary>
struct CoverageBitmap
{
    std::vector<std::byte> alpha;
    unsigned int width = 0;
    unsigned int height = 0;
    float originX = 0.0f;
    float originY = 0.0f;

    [[nodiscard]] bool IsEmpty() const { return width == 0 || height == 0; }
};

/// <summary>주사 변환 방식이다. 어느 쪽이 나은지는 재어서 정한다.</summary>
enum class ScanConversion : unsigned char
{
    /// <summary>
    /// 가로는 정확히, 세로는 표본으로. 한 부주사선이 만드는 구간의 양 끝을 픽셀에 걸친 <b>넓이
    /// 그대로</b> 더한다. 가로 해상도가 표본 수에 걸리지 않는 것이 요점이다.
    /// </summary>
    AnalyticHorizontal,
    /// <summary>
    /// 가로도 세로도 표본으로. 픽셀 안에 격자를 깔고 안에 든 표본을 센다. 가장 단순해서 기준으로
    /// 삼기 좋고, 같은 품질을 내려면 표본이 훨씬 많아야 한다.
    /// </summary>
    Supersampled,
};

/// <summary>주사 변환에 주는 값들이다.</summary>
struct RasterizerSettings
{
    ScanConversion method = ScanConversion::AnalyticHorizontal;
    /// <summary>픽셀 하나를 세로로 몇 겹으로 훑는가.</summary>
    unsigned int verticalSamples = 4;
    /// <summary>가로 표본 수다. <see cref="ScanConversion::Supersampled"/>일 때만 쓰인다.</summary>
    unsigned int horizontalSamples = 4;
};

/// <summary>
/// 윤곽선을 커버리지 픽셀로 바꾼다. 채우기 규칙은 0이 아닌 감김수다.
///
/// 힌팅은 하지 않는다. 폰트가 들고 있는 힌팅 명령은 작은 크기에서 획을 픽셀 격자에 맞춰
/// 붙이는 프로그램인데, 그것을 해석하려면 TrueType 인터프리터가 통째로 더 붙는다. 그래서 이
/// 래스터라이저의 출력은 DirectWrite의 것과 <b>체계적으로 다르며</b>, 특히 작은 크기에서
/// 그렇다.
/// </summary>
/// <param name="outline">폰트 단위의 윤곽선이다. y는 위로 간다.</param>
/// <param name="pixelsPerUnit">폰트 단위 하나가 몇 픽셀인가. <c>크기 / unitsPerEm</c>이다.</param>
/// <param name="settings">주사 변환 방식과 표본 수다.</param>
/// <param name="bitmap">커버리지와 배치 오프셋을 받는다.</param>
/// <returns>만들었으면 true다. 잉크 없는 글리프도 성공이며 크기가 0이다.</returns>
[[nodiscard]] bool RasterizeGlyphOutline(
    const GlyphOutline& outline,
    float pixelsPerUnit,
    const RasterizerSettings& settings,
    CoverageBitmap& bitmap);

}
