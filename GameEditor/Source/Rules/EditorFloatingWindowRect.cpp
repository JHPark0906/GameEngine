#include "Rules/EditorFloatingWindowRect.h"

#include <algorithm>

namespace GameEditor
{

GameEngine::UI::UIRect ClampFloatingWindow(
    const GameEngine::UI::UIRect& rect, const float clientWidth, const float clientHeight,
    const float minimumVisible)
{
    GameEngine::UI::UIRect clamped = rect;
    // 창이 남겨야 할 길이는 창 자신보다 길 수 없다. 작은 창은 통째로 보이는 것이 하한이다.
    const float visible = (std::min)(minimumVisible, clamped.width);
    // 가로: 왼쪽으로는 오른쪽 끝이 화면 안에 남는 만큼, 오른쪽으로는 왼쪽 끝이 남는 만큼.
    const float leftLimit = visible - clamped.width;
    const float rightLimit = (std::max)(clientWidth - visible, leftLimit);
    clamped.x = std::clamp(clamped.x, leftLimit, rightLimit);
    // 세로: 위쪽 변이 화면 안이어야 한다. 위로 넘기면 제목줄이 사라져 잡을 것이 없어진다.
    clamped.y = std::clamp(clamped.y, 0.0f, (std::max)(clientHeight - visible, 0.0f));
    return clamped;
}

}
