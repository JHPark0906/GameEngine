#include "EditorToolbarLayoutTests.h"

#include <array>
#include <iostream>
#include <vector>

#include "Views/EditorDockLayout.h"
#include "Rules/EditorToolbarLayout.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>
    /// 시험용 폭 열둘이다. 재는 것은 창 안에 들어가는가, 겹치지 않는가, 몇 줄이 되는가다.
    /// 실제 툴바가 선언한 버튼들이 빠짐없이 검사되는지는 소스를 읽는 시험이 별도로 확인한다.
    /// </summary>
    constexpr std::array<float, 12> SampleWidths{
        96.0f, 100.0f, 88.0f, 88.0f, 88.0f, 112.0f, 104.0f, 56.0f, 56.0f, 76.0f, 56.0f, 64.0f };

    [[nodiscard]] bool Overlaps(
        const GameEngine::UI::UIRect& a, const GameEngine::UI::UIRect& b)
    {
        return a.x < b.x + b.width && b.x < a.x + a.width &&
            a.y < b.y + b.height && b.y < a.y + a.height;
    }
}

bool RunEditorToolbarLayoutTests()
{
    using GameEditor::ComputeToolbarLayout;
    using GameEditor::ToolbarLayout;
    using GameEditor::ToolbarLayoutMetrics;

    bool passed = true;
    const ToolbarLayoutMetrics metrics;

    // ⑴ 좁은 창에서도 버튼이 사라지지 않는다. 이것이 이 계산이 존재하는 이유다: 200% 배율의
    // 1920 화면에서 창의 논리 폭은 726이고, 접지 않으면 줄 끝의 Play가 창 밖으로 나간다.
    {
        constexpr float NarrowWindow = 726.0f;
        const ToolbarLayout layout =
            ComputeToolbarLayout(SampleWidths, NarrowWindow, metrics);

        passed &= Expect(
            layout.buttons.size() == SampleWidths.size(),
            "every button is placed, however narrow the window");

        bool allInside = true;
        for (const GameEngine::UI::UIRect& rect : layout.buttons)
        {
            if (rect.x < 0.0f || rect.x + rect.width > NarrowWindow)
            {
                std::cerr << "  a button lies outside the window: x=" << rect.x
                          << " width=" << rect.width << " window=" << NarrowWindow << "\n";
                allInside = false;
            }
        }
        passed &= Expect(allInside, "and lies inside it");

        bool noOverlap = true;
        for (std::size_t i = 0; i < layout.buttons.size(); ++i)
        {
            for (std::size_t j = i + 1; j < layout.buttons.size(); ++j)
            {
                if (Overlaps(layout.buttons[i], layout.buttons[j]))
                {
                    std::cerr << "  buttons " << i << " and " << j << " overlap\n";
                    noOverlap = false;
                }
            }
        }
        passed &= Expect(noOverlap, "without landing on top of one another");
        passed &= Expect(layout.rows > 1, "which takes more than one row at this width");
        passed &= Expect(
            layout.height == static_cast<float>(layout.rows) * metrics.rowHeight,
            "and the strip is as tall as the rows it holds");
    }

    // ⑵ 넓은 창에서는 접지 않는다. 접힘은 좁을 때의 대비책이지 새 모양이 아니다.
    {
        const ToolbarLayout layout = ComputeToolbarLayout(SampleWidths, 2000.0f, metrics);
        passed &= Expect(layout.rows == 1, "a wide window keeps the toolbar on one row");
        passed &= Expect(
            layout.height == metrics.rowHeight, "and the strip stays one row tall");
    }

    // ⑶ 여백 산술이다. 첫 버튼은 여백만큼 들어가 있고, 이웃 사이도 여백만큼 벌어져 있으며,
    // 버튼 높이는 줄 높이에서 위아래 여백을 뺀 값이다.
    {
        const ToolbarLayout layout = ComputeToolbarLayout(SampleWidths, 2000.0f, metrics);
        passed &= Expect(
            layout.buttons.front().x == metrics.inset, "the first button is inset from the edge");
        const float gap = layout.buttons[1].x - (layout.buttons[0].x + layout.buttons[0].width);
        passed &= Expect(gap == metrics.inset, "neighbours are one inset apart");
        passed &= Expect(
            layout.buttons.front().height == metrics.rowHeight - metrics.inset * 2.0f,
            "and a button is the row height less the inset above and below");
    }

    // ⑷ 창보다 넓은 버튼 하나는 그래도 놓는다. 줄의 첫 자리에서까지 내리면 줄만 끝없이 늘고
    // 버튼은 어디에도 놓이지 않는다. 넘친 버튼은 눈에 띄지만 없는 버튼은 그렇지 않다.
    {
        constexpr std::array<float, 1> TooWide{ 400.0f };
        const ToolbarLayout layout = ComputeToolbarLayout(TooWide, 100.0f, metrics);
        passed &= Expect(layout.buttons.size() == 1, "a button wider than the window is placed");
        passed &= Expect(layout.rows == 1, "on one row rather than pushed off the end");
    }

    // ⑸ 버튼이 없어도 띠는 있다. 도킹이 받는 높이가 0이 되면 패널들이 띠 자리로 올라온다.
    {
        const ToolbarLayout layout = ComputeToolbarLayout({}, 726.0f, metrics);
        passed &= Expect(layout.buttons.empty(), "no buttons means no rectangles");
        passed &= Expect(
            layout.rows == 1 && layout.height == metrics.rowHeight,
            "but the strip is still one row tall");
    }
    // 도킹이 비워 두는 높이는 툴바가 제공한 높이와 같아야 패널과 툴바가 겹치거나 틈이 생기지 않는다.
    {
        const GameEditor::DockSlotMetrics dock;
        passed &= Expect(
            dock.toolbarHeight == metrics.rowHeight,
            "the dock reserves exactly one toolbar row by default");

        // 접히면 그 값도 함께 자라야 한다. 도킹이 상수를 쥐고 있으면 이 관계가 끊긴다.
        const ToolbarLayout folded = ComputeToolbarLayout(SampleWidths, 726.0f, metrics);
        passed &= Expect(
            folded.height == static_cast<float>(folded.rows) * dock.toolbarHeight,
            "and grows by whole rows when the toolbar folds");
    }

    return passed;
}

static const TestSupport::Registration gEditorToolbarLayoutTests{
    "EditorDocument", "editor toolbar layout tests should pass", RunEditorToolbarLayoutTests };
