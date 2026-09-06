#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace GameEngine::Text
{

/// <summary>
/// 줄바꿈이 고려하는 한 덩어리다. 대개 글자 하나이며, 그보다 잘게 쪼개지지 않는다.
/// </summary>
struct BreakItem
{
    /// <summary>원문에서 이 덩어리가 시작하는 바이트 자리다.</summary>
    std::size_t byteOffset = 0;
    std::size_t byteLength = 0;

    /// <summary>
    /// 이 덩어리의 코드 포인트다.
    ///
    /// 줄바꿈 규칙 자체는 이 값을 쓰고 나면 잊어도 되지만, 폭을 재는 쪽이 곧바로 다시 필요로
    /// 한다 — 어느 글리프인지 물어야 하기 때문이다. 여기 담아 두지 않으면 같은 문자열을 두 번
    /// 디코딩하게 되고, 그 둘이 갈라질 여지가 생긴다.
    /// </summary>
    char32_t codePoint = 0;

    /// <summary>이 덩어리가 차지하는 가로 폭이다. 픽셀이며, 재는 쪽이 채워 준다.</summary>
    float advance = 0.0f;

    /// <summary>이 덩어리 <b>앞에서</b> 줄을 바꿀 수 있는가.</summary>
    bool canBreakBefore = false;

    /// <summary>공백인가. 줄 끝에 남는 공백은 폭에 넣지 않는다.</summary>
    bool isSpace = false;

    /// <summary>줄바꿈 문자인가. 폭과 무관하게 여기서 줄이 끝난다.</summary>
    bool isMandatoryBreak = false;
};

/// <summary>한 줄이 담는 덩어리들의 범위다.</summary>
struct LineRange
{
    std::size_t firstItem = 0;
    std::size_t itemCount = 0;
    /// <summary>줄 끝의 공백을 뺀 폭이다. 정렬은 이 값으로 한다.</summary>
    float width = 0.0f;
};

/// <summary>
/// 글자를 줄로 나눈다.
///
/// <b>폰트를 모른다.</b> 폭은 이미 재어 <see cref="BreakItem::advance"/>에 담겨 오고, 여기서
/// 하는 일은 그 수들 사이의 순수한 판단이다. 재는 일과 자르는 일을 나누는 것은 이 저장소의
/// <c>UI::FitTextToWidth</c>가 이미 쓰는 모양이며, 그래야 규칙을 폰트 없이 시험할 수 있다.
///
/// <paramref name="maxWidth"/>가 0이면 넘침으로 인한 줄바꿈은 하지 않는다 — 명시적 줄바꿈만
/// 남는다.
///
/// 한 덩어리가 혼자서도 폭을 넘으면 그 줄에 그대로 둔다. 넘친다고 버리면 글자가 사라지고,
/// 더 쪼개려 해도 쪼갤 것이 없다.
/// </summary>
[[nodiscard]] std::vector<LineRange> BreakIntoLines(
    std::span<const BreakItem> items, float maxWidth);

/// <summary>
/// UTF-8 글자들에 줄바꿈 기회를 표시한다. 폭은 채우지 않는다 — 재는 쪽이 채운다.
///
/// <b>공백</b>과 <b>한글·한자 음절 경계</b>에서 자르고,
/// 유니코드 줄바꿈 알고리즘(UAX #14) 전체를 흉내 내지는 않는다. 라틴 낱말은 공백에서만
/// 갈라지고 한글은 글자마다 갈라질 수 있는데, 그 둘이 이 편집기가 실제로 그리는 것의 거의
/// 전부다.
///
/// 다만 <b>금칙</b>은 뺄 수 없다. 닫는 괄호나 마침표가 줄 첫머리에 혼자 떨어지는 것, 여는
/// 괄호가 줄 끝에 남는 것은 한글 조판에서 눈에 띄게 잘못돼 보인다 — 「글자마다 자를 수 있다」를
/// 규칙 그대로 적용하면 반드시 일어나는 일이라, 이것까지가 최소한이다.
/// </summary>
/// <returns>글자마다 하나씩. 입력이 UTF-8이 아니면 비어 있다.</returns>
[[nodiscard]] std::vector<BreakItem> FindBreakOpportunities(std::string_view text);

}
