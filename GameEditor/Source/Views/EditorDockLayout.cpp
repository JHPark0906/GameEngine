#include "Views/EditorDockLayout.h"

#include <algorithm>

namespace GameEditor
{

std::array<GameEngine::UI::UIRect, DockSlotCount> ComputeDockSlotRects(
    const float width, const float height, const DockSlotMetrics& metrics)
{
    const float leftWidth = std::clamp(
        width * metrics.leftFraction, metrics.leftMinimum, metrics.leftMaximum);
    const float rightWidth = std::clamp(
        width * metrics.rightFraction, metrics.rightMinimum, metrics.rightMaximum);
    const float centerWidth =
        (std::max)(width - leftWidth - rightWidth, metrics.minimumExtent);

    const float bodyTop = metrics.toolbarHeight;
    const float bodyHeight = (std::max)(height - bodyTop, metrics.minimumExtent);

    // 좌측의 세 자리. 마지막 자리는 남는 만큼을 받는다 — 앞의 둘을 반올림하고 남은 픽셀이
    // 사라지지 않게 하는 자리이기도 하다.
    const float hierarchyHeight = bodyHeight * metrics.hierarchyFraction;
    const float inspectorHeight = bodyHeight * metrics.inspectorFraction;
    const float sceneHeight = bodyHeight * metrics.sceneFraction;
    const float gameHeight = bodyHeight * metrics.gameFraction;
    const float rightX = leftWidth + centerWidth;

    return { {
        { 0.0f, bodyTop, leftWidth, hierarchyHeight },
        { 0.0f, bodyTop + hierarchyHeight, leftWidth, inspectorHeight },
        { 0.0f, bodyTop + hierarchyHeight + inspectorHeight, leftWidth,
          bodyHeight - hierarchyHeight - inspectorHeight },
        { leftWidth, bodyTop, centerWidth, sceneHeight },
        { leftWidth, bodyTop + sceneHeight, centerWidth, bodyHeight - sceneHeight },
        { rightX, bodyTop, rightWidth, gameHeight },
        { rightX, bodyTop + gameHeight, rightWidth, bodyHeight - gameHeight },
    } };
}

float ComputePaletteBoxHeight(
    const float availableHeight, const int totalRows, const float cellSize)
{
    // 칸이 남은 자리보다 적으면 칸만큼만 쓴다. 그보다 많으면 남은 자리를 다 쓰고, 넘치는 만큼은
    // 팔레트 자신이 스크롤한다 — 여기에 상한은 없다.
    const float rowsHeight = static_cast<float>((std::max)(totalRows, 0)) * cellSize;
    return (std::min)(rowsHeight, (std::max)(availableHeight, cellSize));
}

}
