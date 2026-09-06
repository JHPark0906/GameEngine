#include "Rules/EditorToolbarLayout.h"

#include <algorithm>
#include <vector>

namespace GameEditor
{

ToolbarLayout ComputeToolbarLayout(
    const std::span<const float> buttonWidths, const float availableWidth,
    const ToolbarLayoutMetrics& metrics)
{
    ToolbarLayout layout;
    layout.buttons.reserve(buttonWidths.size());

    int row = 0;
    float x = metrics.inset;
    for (const float width : buttonWidths)
    {
        // 이 줄에 들어가지 않으면 다음 줄로 내린다. 줄의 첫 버튼은 내리지 않는다 — 창이 그
        // 버튼 하나보다 좁을 때 무한히 줄만 늘어나기 때문이고, 그때는 넘치더라도 놓는 편이
        // 낫다. 넘친 버튼은 눈에 띄지만 없는 버튼은 그렇지 않다.
        const bool isFirstInRow = x <= metrics.inset;
        if (!isFirstInRow && x + width + metrics.inset > availableWidth)
        {
            ++row;
            x = metrics.inset;
        }

        GameEngine::UI::UIRect rect;
        rect.x = x;
        rect.y = static_cast<float>(row) * metrics.rowHeight + metrics.inset;
        rect.width = width;
        rect.height = (std::max)(metrics.rowHeight - metrics.inset * 2.0f, 0.0f);
        layout.buttons.push_back(rect);

        x += width + metrics.inset;
    }

    layout.rows = row + 1;
    layout.height = static_cast<float>(layout.rows) * metrics.rowHeight;
    return layout;
}


ToolbarLayout ComputeToolbarRowRects(
    const std::span<const float> desiredWidths, const float availableWidth, const float scale,
    const ToolbarLayoutMetrics& metrics)
{
    const float pixelsPerLogical = scale > 0.0f ? scale : 1.0f;

    std::vector<float> logicalWidths;
    logicalWidths.reserve(desiredWidths.size());
    for (const float desired : desiredWidths)
    {
        // 아직 재지 못한 프레임에서는 최소 폭이 답이다. 0으로 두면 버튼이 한 프레임 사라진다.
        logicalWidths.push_back(
            (std::max)(desired / pixelsPerLogical, metrics.minimumButtonWidth));
    }
    return ComputeToolbarLayout(logicalWidths, availableWidth / pixelsPerLogical, metrics);
}

}
