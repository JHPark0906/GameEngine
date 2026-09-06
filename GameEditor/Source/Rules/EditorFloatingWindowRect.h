#pragma once

// editor-layer: 0 (Rules)

#include "UI/UIContext.h"

namespace GameEditor
{

/// <summary>
/// 떠 있는 창을 화면 안으로 물린다. 논리 픽셀로 재고 논리 픽셀로 답한다.
///
/// 창은 사람이 끄는 것이라 화면 밖으로 나갈 수 있고, 제목줄이 나가면 다시 잡을 수 없다 —
/// 되돌릴 방법이 없는 상태라 창을 잃는 것과 같다. 그래서 물리는 기준은 창 전체가 아니라
/// <b>제목줄을 다시 잡을 수 있는가</b>이다: 가로로는 창의 일부가 남으면 되고, 세로로는 위쪽
/// 변 — 제목줄이 있는 변 — 이 화면 안이어야 한다. 아래로는 머리만 남아도 잡을 수 있으므로
/// 끝까지 허용한다.
///
/// UI를 모르는 순수한 계산이라 창 없이 시험된다. 도킹 슬롯 계산과 같은 이유다: 화면 크기가
/// 바뀌는 자리마다 "그때 창이 어디로 가는가"를 눈이 아니라 수로 답할 수 있어야 한다.
/// </summary>
/// <param name="rect">창의 자리다.</param>
/// <param name="clientWidth">그릴 수 있는 자리의 폭이다.</param>
/// <param name="clientHeight">그릴 수 있는 자리의 높이다.</param>
/// <param name="minimumVisible">화면 안에 남아야 하는 창의 가로 길이다.</param>
[[nodiscard]] GameEngine::UI::UIRect ClampFloatingWindow(
    const GameEngine::UI::UIRect& rect, float clientWidth, float clientHeight,
    float minimumVisible);

}
