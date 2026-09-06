#pragma once

// editor-layer: 0 (Rules)

#include "UI/UIContext.h"

namespace GameEditor
{

/// <summary>
/// 펼친 목록이 갖는 최소 폭이다. 글자를 재지 못한 프레임에서도 목록이 보이도록 하는 값이며,
/// 「무엇이 들어갈 만큼」이 아니라 「거기 무언가 열렸음을 알아볼 만큼」이다. 논리 픽셀이다.
/// </summary>
inline constexpr float MinimumMenuListWidth = 96.0f;

/// <summary>펼친 목록을 어디에 놓을지다.</summary>
struct MenuListPlacement
{
    /// <summary>목록이 덮는 사각형이다.</summary>
    GameEngine::UI::UIRect panel;

    /// <summary>
    /// 목록이 창 안에 다 들어갔는지다. 거짓이면 오른쪽이 잘리므로 단축키는 그리지 않는다 —
    /// 반쯤 잘린 단축키는 잘못된 키를 읽히게 하고, 그것은 안 보이는 것보다 나쁘다.
    /// </summary>
    bool shortcutsFit = true;
};

/// <summary>
/// 머리줄 아래에 펼친 목록을 놓는다.
///
/// <b>폭을 만들지 않고 받는다.</b> 목록의 폭은 세로 <c>LayoutGroup</c>이 가장 넓은 줄에서
/// 얻은 것이고, 이 함수가 하는 일은 그 폭에 하한 둘을 씌우고 창 안으로 물리는 것뿐이다.
/// 글자에서 크기를 얻는 길은 엔진에 하나뿐이어야 하며, 여기서 여백을 더해 폭을 만들면 그것이
/// 두 번째 길이 된다.
///
/// 목록은 머리줄의 왼쪽 끝에 맞춰 열린다 — 사람이 누른 자리가 곧 목록이 나오는 자리라야 시선이
/// 이어진다. 다만 오른쪽 끝의 메뉴는 그대로 열면 창 밖으로 나가므로, 그때만 <b>들어갈 만큼</b>
/// 왼쪽으로 민다. 오른쪽 정렬로 뒤집지 않는 이유는 그 규칙이 폭에 따라 목록의 자리를 크게
/// 옮겨서, 같은 메뉴가 창 크기에 따라 다른 데서 열리는 것처럼 보이기 때문이다.
///
/// 창이 목록보다 좁으면 더 밀 곳이 없다. 그때는 왼쪽에 붙여 <b>단축키를 잃고 이름을 남긴다</b> —
/// 어느 쪽을 잘라도 잘리는 화면에서, 무엇을 고르는지 말해 주는 쪽은 이름이다.
/// </summary>
/// <param name="headingRect">이 목록을 연 머리줄의 사각형이다.</param>
/// <param name="listWidth">LayoutGroup이 낸 목록의 요구 폭이다. 배율이 이미 곱해져 있다.</param>
/// <param name="listHeight">LayoutGroup이 낸 목록의 요구 높이다. 그대로 쓰인다.</param>
/// <param name="clientWidth">창의 가로 폭이다. 목록이 이 밖으로 나가지 않는다.</param>
/// <param name="scale">이 면의 픽셀 배율이다. 최소 폭에만 곱해진다.</param>
[[nodiscard]] MenuListPlacement PlaceMenuList(
    const GameEngine::UI::UIRect& headingRect, float listWidth, float listHeight,
    float clientWidth, float scale);

}
