#include "Rules/EditorMenuBarLayout.h"

#include <algorithm>

namespace GameEditor
{

MenuListPlacement PlaceMenuList(
    const GameEngine::UI::UIRect& headingRect, const float listWidth, const float listHeight,
    const float clientWidth, const float scale)
{
    // 하한 둘. 목록이 머리줄보다 좁으면 눌린 머리줄의 오른쪽이 목록 밖으로 튀어나와 무엇이
    // 열려 있는지가 흐려지고, 아무것도 재지 못한 프레임에서 폭이 0이면 열린 메뉴가 아예
    // 없는 것처럼 보인다.
    const float width =
        (std::max)({ listWidth, headingRect.width, MinimumMenuListWidth * scale });

    MenuListPlacement placement;
    placement.panel = { (std::max)(0.0f, (std::min)(headingRect.x, clientWidth - width)),
                        headingRect.y + headingRect.height, width, listHeight };
    placement.shortcutsFit = width <= clientWidth;
    return placement;
}

}
