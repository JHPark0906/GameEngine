#include <array>
#include <cstddef>
#include <iostream>

#include "Views/EditorDockLayout.h"

#include "DockSlotCountTests.h"
#include "TestSupport.h"

namespace
{

/// <summary>한 창 크기에서 슬롯들이 컬럼별로 몇 자리씩 나뉘는지 센다.</summary>
/// <remarks>
/// 컬럼은 x로 갈린다: 슬롯은 좌·중·우 세 줄기로만 놓이므로, 서로 다른 x가 몇 개인지가
/// 곧 컬럼 수이고 같은 x를 공유하는 개수가 그 컬럼의 자리 수다. 여기에 배율은 변수가
/// 아니다 — <c>ComputeDockSlotRects</c>는 이미 배율이 곱해진 픽셀을 받으므로 배율은
/// 자리 수를 바꾸지 못하고, 자리 수를 바꿀 수 있는 것은 창 크기(한계에 걸리는지)다.
/// </remarks>
bool CountsMatchTheComposition(const float width, const float height, const char* where)
{
    using GameEditor::CenterDockSlotCount;
    using GameEditor::DockSlotCount;
    using GameEditor::LeftDockSlotCount;
    using GameEditor::RightDockSlotCount;

    const std::array<GameEngine::UI::UIRect, DockSlotCount> slots =
        GameEditor::ComputeDockSlotRects(width, height, GameEditor::DockSlotMetrics{});

    std::array<float, DockSlotCount> columnX{};
    std::array<std::size_t, DockSlotCount> columnCount{};
    std::size_t columns = 0;
    for (const GameEngine::UI::UIRect& slot : slots)
    {
        std::size_t column = 0;
        while (column < columns && columnX[column] != slot.x)
        {
            ++column;
        }
        if (column == columns)
        {
            columnX[columns] = slot.x;
            ++columns;
        }
        ++columnCount[column];
    }

    bool passed = TestSupport::Expect(columns == 3, where);
    if (!passed)
    {
        std::cout << "  columns: " << columns << " (expected three)\n";
        return false;
    }

    passed = TestSupport::Expect(columnCount[0] == LeftDockSlotCount, where) && passed;
    passed = TestSupport::Expect(columnCount[1] == CenterDockSlotCount, where) && passed;
    passed = TestSupport::Expect(columnCount[2] == RightDockSlotCount, where) && passed;
    if (!passed)
    {
        std::cout << "  slots per column: " << columnCount[0] << ", " << columnCount[1]
                  << ", " << columnCount[2] << " (expected " << LeftDockSlotCount << ", "
                  << CenterDockSlotCount << ", " << RightDockSlotCount << ")\n";
    }
    return passed;
}

}

bool RunDockSlotCountTests()
{
    std::cout << "running dock slot count tests\n";
    bool passed = true;

    // 넉넉한 창과, 좌·우 컬럼이 하한에 걸릴 만큼 좁은 창. 한계에 걸려도 컬럼이 사라지지
    // 않아야 한다 — 사라지면 슬롯 하나가 다른 컬럼으로 접히고 합만 그대로 남는다.
    passed = CountsMatchTheComposition(
        1280.0f, 720.0f, "a roomy window should split into the composed columns") && passed;
    passed = CountsMatchTheComposition(
        520.0f, 360.0f, "a cramped window should still split into the composed columns")
        && passed;

    return passed;
}

static const TestSupport::Registration gDockSlotCountTests{
    "EditorDocument", "dock slot count tests should pass", RunDockSlotCountTests };
