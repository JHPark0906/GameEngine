#include "Rules/EditorPlayInputFocus.h"

namespace GameEditor
{

PlayInputFocusResult ResolvePlayInputFocus(
    const PlayInputFocusFrame& frame, const bool wasCaptured)
{
    PlayInputFocusResult result;

    // 놓는 조건이 먼저다. 플레이가 끝났거나 창이 뒤로 물러났으면 잡고 있을 이유가 사라졌고,
    // 그 프레임에 눌린 것이 있어도 그것은 게임의 것이 아니다.
    if (!frame.isPlaying || !frame.windowHasFocus)
    {
        return result;
    }

    if (wasCaptured)
    {
        // Esc가 이긴다. 이 프레임에서 게임은 그 키를 보지 못한다.
        result.captured = !frame.escapePressed;
        return result;
    }

    if (frame.pressedInsideGameView)
    {
        result.captured = true;
        // 잡는 클릭은 삼킨다.
        result.swallowClick = true;
    }
    return result;
}

}
