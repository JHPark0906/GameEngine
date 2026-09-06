#pragma once

#include <vector>

namespace GameEngine::Text
{

/// <summary>윤곽선 위의 점이다. 폰트 단위이며 y는 위로 간다.</summary>
struct OutlinePoint
{
    float x = 0.0f;
    float y = 0.0f;
};

/// <summary>
/// 윤곽선의 한 도막이다. 시작점은 앞 도막의 끝이므로 담지 않는다.
///
/// 도막은 <b>언제나 3차</b>다. 직선도 2차도 3차로 승격해 담는다.
/// </summary>
struct OutlineSegment
{
    OutlinePoint control1;
    OutlinePoint control2;
    OutlinePoint end;
};

/// <summary>닫힌 고리 하나다. 마지막 도막의 끝은 <see cref="start"/>로 돌아온다.</summary>
struct OutlineContour
{
    OutlinePoint start;
    std::vector<OutlineSegment> segments;
};

/// <summary>
/// 글리프 하나의 윤곽선이다. 폰트 단위이고, 채우기 규칙은 0이 아닌 감김수다.
///
/// <b>모든 도막을 3차로 담는 것이 이 타입의 결정이다.</b> TrueType의 glyf는 2차 베지에고
/// OpenType의 CFF는 3차인데, 두 형식을 다 지원하기로 한 이상 래스터라이저가 둘을 다 알거나
/// 하나로 모으거나 둘 중 하나다. 여기서는 모은다: 2차는 제어점 하나를 둘로 올려 3차로 바꿔
/// 담고(<c>C1 = P0 + 2/3(Q-P0)</c>, <c>C2 = P1 + 2/3(Q-P1)</c> — 정확한 변환이지 근사가
/// 아니다), 직선은 제어점을 선 위 3분의 1과 3분의 2에 놓아 담는다.
///
/// 그래서 <b>래스터라이저는 3차 하나만 안다.</b> 반대로 2차만 담는 표현을 골랐다면 CFF를
/// 더하는 날 그것을 다시 쓰게 되고, 그것이 이 작업에서 가장 비싼 실수가 된다 — 3차를 2차로
/// 낮추는 것은 승격과 달리 <b>근사</b>라서 글자 모양이 조금씩 달라지고, 그 차이는 DirectWrite와
/// 픽셀을 견주는 자리에서 원인을 알 수 없는 오차로 나타난다.
/// </summary>
struct GlyphOutline
{
    std::vector<OutlineContour> contours;

    [[nodiscard]] bool IsEmpty() const { return contours.empty(); }
};

/// <summary>2차 베지에를 같은 곡선의 3차로 올린다. 근사가 아니라 항등이다.</summary>
[[nodiscard]] inline OutlineSegment PromoteQuadratic(
    const OutlinePoint start, const OutlinePoint control, const OutlinePoint end)
{
    constexpr float TwoThirds = 2.0f / 3.0f;
    return OutlineSegment{
        OutlinePoint{ start.x + TwoThirds * (control.x - start.x),
                      start.y + TwoThirds * (control.y - start.y) },
        OutlinePoint{ end.x + TwoThirds * (control.x - end.x),
                      end.y + TwoThirds * (control.y - end.y) },
        end };
}

/// <summary>직선을 3차로 담는다. 제어점을 선 위에 두면 곡선은 그 직선이다.</summary>
[[nodiscard]] inline OutlineSegment PromoteLine(const OutlinePoint start, const OutlinePoint end)
{
    constexpr float OneThird = 1.0f / 3.0f;
    constexpr float TwoThirds = 2.0f / 3.0f;
    return OutlineSegment{
        OutlinePoint{ start.x + OneThird * (end.x - start.x),
                      start.y + OneThird * (end.y - start.y) },
        OutlinePoint{ start.x + TwoThirds * (end.x - start.x),
                      start.y + TwoThirds * (end.y - start.y) },
        end };
}

}
