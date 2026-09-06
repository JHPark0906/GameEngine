#pragma once

// editor-layer: 0 (Rules)

#include <span>
#include <vector>

#include "UI/UIContext.h"

namespace GameEditor
{

/// <summary>
/// 툴바 한 줄의 여백과 높이다.
///
/// 값이 아니라 관계로 읽어야 하는 자리가 하나 있다: <see cref="rowHeight"/>는 툴바가 자기
/// 높이를 말할 때 쓰는 한 줄의 높이이며, 도킹이 비워 두는 높이는 그것에 줄 수를 곱한 값이다.
/// 두 곳이 각자 상수를 들고 있으면 접힘이 생기는 순간 갈라지고, 갈라진 결과는 패널이 툴바
/// 아래에 깔리거나 띠 밑에 빈 줄이 생기는 것으로 나타난다.
/// </summary>
struct ToolbarLayoutMetrics
{
    /// <summary>버튼 사이와 띠 가장자리의 여백이다.</summary>
    float inset = 4.0f;

    /// <summary>한 줄의 높이다. 버튼은 이 높이에서 위아래 여백을 뺀 만큼을 차지한다.</summary>
    float rowHeight = 32.0f;

    /// <summary>
    /// 글자를 재지 못했을 때 버튼이 갖는 폭이다. 첫 프레임과 글꼴을 열지 못한 화면이 그런
    /// 자리이며, 이 값은 「무엇이 들어갈 만큼」이 아니라 「눌러 볼 수 있을 만큼」이다.
    /// </summary>
    float minimumButtonWidth = 48.0f;
};

/// <summary>
/// 접은 결과다. 사각형은 버튼 순서 그대로이며, 높이는 도킹이 비워 두어야 할 값이다.
/// </summary>
struct ToolbarLayout
{
    /// <summary>버튼 하나마다의 사각형이다. 입력한 폭 목록과 같은 순서·같은 개수다.</summary>
    std::vector<GameEngine::UI::UIRect> buttons;

    /// <summary>버튼이 놓인 줄 수다. 버튼이 없으면 한 줄이다 — 띠는 여전히 존재한다.</summary>
    int rows = 1;

    /// <summary>띠 전체의 높이다. 줄 수 × 줄 높이이며, 도킹이 받는 값이 이것이다.</summary>
    float height = 0.0f;
};

/// <summary>
/// 버튼들을 주어진 폭 안에 늘어놓고, 넘치면 다음 줄로 접는다.
///
/// 접기가 필요한 이유는 툴바가 자라기 때문이다. 버튼 하나를 더하는 결정은 언제나 국소적으로
/// 타당해 보이고 그래서 줄은 계속 길어지는데, 창 폭에는 그런 여유가 없다 — 200% 배율의 1920
/// 화면에서 열두 개짜리 줄은 최대화해도 들어가지 않는다. 넘친 자리에 그대로 놓는 배치는 줄
/// <b>끝에</b> 있는 버튼을 화면 밖으로 밀어내므로, 사라지는 것은 방금 더한 버튼이 아니라 원래
/// 거기 있던 기능이다.
///
/// 잘라내지 않고 접는 이유도 같다. 폭이 모자랄 때 버튼을 숨기면 사람은 그것이 사라졌다고
/// 읽지 자기 창이 좁다고 읽지 않는다.
///
/// 이 계산은 UI를 모른다. 폭 목록과 창 폭만 받아 사각형을 내므로 창 없이 시험할 수 있고,
/// 나중에 가로 배치가 컴포넌트로 필요해지면 그 컴포넌트가 이 함수를 부르면 된다.
/// </summary>
/// <param name="buttonWidths">버튼들의 폭이다. 배율이 이미 곱해진 픽셀이어야 한다.</param>
/// <param name="availableWidth">띠가 쓸 수 있는 폭이다.</param>
/// <param name="metrics">여백과 줄 높이다.</param>
/// <returns>버튼 사각형들과 그로부터 정해진 띠의 높이다.</returns>
[[nodiscard]] ToolbarLayout ComputeToolbarLayout(
    std::span<const float> buttonWidths, float availableWidth,
    const ToolbarLayoutMetrics& metrics);

/// <summary>
/// 재기가 낸 폭들을 오프셋으로 쓸 사각형으로 바꾼다. 접힘까지 여기서 끝난다.
///
/// 단위를 맞추는 자리가 여기 하나뿐이어야 한다. 요구 크기는 배율이 곱해진 픽셀로 오고 오프셋은
/// 논리 픽셀로 선언되므로, 그 환산을 부르는 쪽마다 하면 한 곳에서 한 번 더 곱해도 아무도
/// 모른다 — 화면에서는 버튼이 배율만큼 넓어질 뿐이라 「글꼴이 큰가」로 보인다.
/// </summary>
/// <param name="desiredWidths">각 버튼이 요구한 폭이다. 배율이 곱해진 픽셀이다.</param>
/// <param name="availableWidth">띠가 쓸 수 있는 폭이다. 이것도 픽셀이다.</param>
/// <param name="scale">이 면의 픽셀 배율이다.</param>
/// <param name="metrics">여백과 줄 높이다. 논리 픽셀이다.</param>
/// <returns>논리 픽셀의 사각형들이다. 오프셋에 그대로 쓸 수 있다.</returns>
[[nodiscard]] ToolbarLayout ComputeToolbarRowRects(
    std::span<const float> desiredWidths, float availableWidth, float scale,
    const ToolbarLayoutMetrics& metrics);

}
