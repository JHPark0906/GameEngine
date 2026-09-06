#include <cstddef>
#include <iostream>

#include "../GameEngine/UI/PanelSlotLayout.h"

#include "PanelSlotLayoutTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>모든 슬롯에 서로 다른 패널이 정확히 하나씩 있는지다 — 배정의 순열 불변식.</summary>
    bool IsPermutation(const GameEngine::UI::PanelSlotLayout& layout)
    {
        for (std::size_t slot = 0; slot < layout.GetSlotCount(); ++slot)
        {
            const std::size_t panel = layout.GetPanelInSlot(slot);
            if (panel >= layout.GetSlotCount() || layout.GetSlotOfPanel(panel) != slot)
            {
                return false;
            }
        }
        return true;
    }

    /// <summary>기본 배정은 패널 i가 슬롯 i다 — 코드가 선언한 순서가 곧 첫 화면이다.</summary>
    bool RunDefaultAssignmentTests()
    {
        const GameEngine::UI::PanelSlotLayout layout(6);
        bool identity = layout.GetSlotCount() == 6;
        for (std::size_t index = 0; index < 6; ++index)
        {
            identity = identity && layout.GetSlotOfPanel(index) == index &&
                layout.GetPanelInSlot(index) == index;
        }
        const GameEngine::UI::PanelSlotLayout empty;
        return Expect(identity, "a fresh layout should place panel i in slot i") &&
            Expect(empty.GetSlotCount() == 0, "a default-constructed layout should be empty");
    }

    /// <summary>스왑은 두 패널의 슬롯을 맞바꾸고, 나머지 패널은 건드리지 않는다.</summary>
    bool RunSwapTests()
    {
        GameEngine::UI::PanelSlotLayout layout(6);
        const bool swapped = layout.SwapPanels(0, 5);
        const bool exchanged = layout.GetSlotOfPanel(0) == 5 && layout.GetSlotOfPanel(5) == 0 &&
            layout.GetPanelInSlot(0) == 5 && layout.GetPanelInSlot(5) == 0;
        bool othersUntouched = true;
        for (std::size_t index = 1; index < 5; ++index)
        {
            othersUntouched = othersUntouched && layout.GetSlotOfPanel(index) == index;
        }

        // 같은 스왑을 한 번 더 하면 원래 배정으로 돌아온다.
        const bool swappedBack = layout.SwapPanels(0, 5) &&
            layout.GetSlotOfPanel(0) == 0 && layout.GetSlotOfPanel(5) == 5;

        // 제자리 드롭: 유효하지만 아무것도 바뀌지 않는다.
        const bool selfSwap = layout.SwapPanels(2, 2) && layout.GetSlotOfPanel(2) == 2;

        return Expect(swapped && exchanged, "swapping two panels should exchange their slots") &&
            Expect(othersUntouched, "a swap should not move any other panel") &&
            Expect(swappedBack, "swapping the same pair again should restore the assignment") &&
            Expect(selfSwap, "swapping a panel with itself should be a valid no-op");
    }

    /// <summary>범위 밖 번호는 배정을 건드리지 못하고, 조회는 슬롯 수를 답한다.</summary>
    bool RunOutOfRangeTests()
    {
        GameEngine::UI::PanelSlotLayout layout(3);
        const bool rejected = !layout.SwapPanels(0, 3) && !layout.SwapPanels(7, 1);
        bool untouched = true;
        for (std::size_t index = 0; index < 3; ++index)
        {
            untouched = untouched && layout.GetSlotOfPanel(index) == index;
        }
        const bool lookupAnswersCount =
            layout.GetSlotOfPanel(3) == 3 && layout.GetPanelInSlot(9) == 3;
        return Expect(rejected, "an out-of-range swap should be rejected") &&
            Expect(untouched, "a rejected swap should not change the assignment") &&
            Expect(lookupAnswersCount, "an out-of-range lookup should answer the slot count");
    }

    /// <summary>스왑을 거듭해도 배정은 순열로 남는다 — 슬롯 하나에 패널 하나.</summary>
    bool RunPermutationInvariantTests()
    {
        GameEngine::UI::PanelSlotLayout layout(6);
        static_cast<void>(layout.SwapPanels(0, 3));
        static_cast<void>(layout.SwapPanels(3, 5));
        static_cast<void>(layout.SwapPanels(1, 0));
        static_cast<void>(layout.SwapPanels(4, 2));
        return Expect(
            IsPermutation(layout),
            "repeated swaps should keep exactly one panel in every slot");
    }

    /// <summary>
    /// 저장된 배정의 복원 경로다: 올바른 순열은 통째로 받아들이고, 크기가 다르거나 순열이 아닌
    /// 값 — 손상된 설정 파일 — 은 배정을 건드리지 못한다.
    /// </summary>
    bool RunSetAssignmentTests()
    {
        GameEngine::UI::PanelSlotLayout layout(4);
        const bool applied = layout.TrySetAssignment({ 2, 0, 3, 1 });
        const bool matches = layout.GetPanelInSlot(0) == 2 && layout.GetPanelInSlot(1) == 0 &&
            layout.GetPanelInSlot(2) == 3 && layout.GetPanelInSlot(3) == 1;

        const bool wrongSizeRejected = !layout.TrySetAssignment({ 0, 1, 2 }) &&
            !layout.TrySetAssignment({});
        const bool duplicateRejected = !layout.TrySetAssignment({ 0, 1, 1, 3 });
        const bool outOfRangeRejected = !layout.TrySetAssignment({ 0, 1, 2, 4 });
        const bool unchangedAfterRejects = layout.GetPanelInSlot(0) == 2 &&
            IsPermutation(layout);

        return Expect(applied && matches, "a valid assignment should be applied slot by slot") &&
            Expect(wrongSizeRejected, "an assignment of the wrong size should be rejected") &&
            Expect(duplicateRejected, "an assignment repeating a panel should be rejected") &&
            Expect(outOfRangeRejected, "an assignment naming an unknown panel should be rejected") &&
            Expect(unchangedAfterRejects, "a rejected assignment should not change the layout");
    }
}

bool RunPanelSlotLayoutTests()
{
    return Expect(RunDefaultAssignmentTests(), "panel slot default assignment tests should pass") &&
        Expect(RunSwapTests(), "panel slot swap tests should pass") &&
        Expect(RunOutOfRangeTests(), "panel slot out-of-range tests should pass") &&
        Expect(RunPermutationInvariantTests(), "panel slot permutation tests should pass") &&
        Expect(RunSetAssignmentTests(), "panel slot assignment restore tests should pass");
}

static const TestSupport::Registration gPanelSlotLayoutTests{
    "UIContext", "panel slot layout tests should pass", RunPanelSlotLayoutTests };
