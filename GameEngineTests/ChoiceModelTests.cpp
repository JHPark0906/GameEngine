#include "ChoiceModelTests.h"

#include <cstddef>
#include <iostream>

#include "UIModel/ChoiceModel.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
}

/// <summary>
/// 선택 목록의 상태 기계다. 화면 없이 인덱스만으로 선다 — 즉시 모드 인스펙터와 유지 모드
/// Dropdown이 같은 규칙을 쓰므로, 여기서 고정한 것이 두 UI의 동작이다.
/// </summary>
bool RunChoiceModelTests()
{
    using GameEngine::UIModel::ChoiceModel;
    constexpr std::size_t Count = 4;

    ChoiceModel model;
    if (!Expect(
            !model.IsOpen() && model.GetSelected() == ChoiceModel::NoSelection,
            "a new model should be closed with nothing selected"))
    {
        return false;
    }

    // 머리 칸을 누르면 펼쳐지고, 선택이 없으니 강조는 첫 항목이다.
    ChoiceModel::Input toggle;
    toggle.toggle = true;
    ChoiceModel::Result result = model.Apply(Count, toggle);
    if (!Expect(
            result.openChanged && model.IsOpen() && model.GetHighlighted() == 0,
            "toggling a closed model should open it with the first item highlighted"))
    {
        return false;
    }

    // 아래로 두 번, 위로 한 번: 강조는 1이다. 끝을 넘지 않는다.
    ChoiceModel::Input down;
    down.moveDown = true;
    static_cast<void>(model.Apply(Count, down));
    static_cast<void>(model.Apply(Count, down));
    ChoiceModel::Input up;
    up.moveUp = true;
    static_cast<void>(model.Apply(Count, up));
    for (int i = 0; i < 10; ++i)
    {
        static_cast<void>(model.Apply(Count, down));
    }
    const bool clampsAtEnd = model.GetHighlighted() == Count - 1;

    // 커서가 놓인 항목이 강조를 가져간다.
    ChoiceModel::Input hover;
    hover.hovered = 1;
    static_cast<void>(model.Apply(Count, hover));
    const bool hoverHighlights = model.GetHighlighted() == 1;

    // 확정: 강조된 항목이 선택되고 접힌다.
    ChoiceModel::Input confirm;
    confirm.confirm = true;
    result = model.Apply(Count, confirm);
    const bool confirmed = result.selectionChanged && result.openChanged && !model.IsOpen() &&
        model.GetSelected() == 1;

    // 다시 펼치면 강조는 선택된 항목에서 시작한다.
    static_cast<void>(model.Apply(Count, toggle));
    const bool reopensAtSelection = model.IsOpen() && model.GetHighlighted() == 1;

    // 취소는 선택을 건드리지 않고 접는다.
    ChoiceModel::Input cancel;
    cancel.cancel = true;
    result = model.Apply(Count, cancel);
    const bool cancelled = !result.selectionChanged && !model.IsOpen() && model.GetSelected() == 1;

    // 클릭은 그 항목을 고르고 접는다. 같은 항목을 다시 골라도 값은 바뀌지 않는다.
    static_cast<void>(model.Apply(Count, toggle));
    ChoiceModel::Input click;
    click.clicked = 3;
    result = model.Apply(Count, click);
    const bool clicked = result.selectionChanged && !model.IsOpen() && model.GetSelected() == 3;
    static_cast<void>(model.Apply(Count, toggle));
    result = model.Apply(Count, click);
    const bool reclickIsNoChange = !result.selectionChanged && !model.IsOpen();

    // 접혀 있을 때의 이동·확정·클릭은 아무것도 하지 않는다.
    result = model.Apply(Count, down);
    ChoiceModel::Result confirmClosed = model.Apply(Count, confirm);
    const bool closedIgnoresNavigation = !result.selectionChanged && !result.openChanged &&
        !confirmClosed.selectionChanged && model.GetSelected() == 3;

    // 목록이 줄어 선택이 범위를 벗어나면 선택이 없어진다. 밖에서 정한 선택도 범위를 지킨다.
    model.ClampTo(2);
    const bool shrinkClearsSelection = model.GetSelected() == ChoiceModel::NoSelection;
    model.SetSelected(1, 2);
    const bool setWithinRange = model.GetSelected() == 1;
    model.SetSelected(7, 2);
    const bool setOutOfRangeClears = model.GetSelected() == ChoiceModel::NoSelection;

    // 빈 목록은 펼쳐도 고를 것이 없다.
    ChoiceModel empty;
    static_cast<void>(empty.Apply(0, toggle));
    const ChoiceModel::Result emptyConfirm = empty.Apply(0, confirm);
    const bool emptyChoosesNothing =
        !emptyConfirm.selectionChanged && empty.GetSelected() == ChoiceModel::NoSelection;

    return Expect(clampsAtEnd, "moving down should stop at the last item") &&
        Expect(hoverHighlights, "hovering an item should highlight it") &&
        Expect(confirmed, "confirm should select the highlighted item and close") &&
        Expect(reopensAtSelection, "reopening should highlight the selected item") &&
        Expect(cancelled, "cancel should close without changing the selection") &&
        Expect(clicked, "clicking an item should select it and close") &&
        Expect(reclickIsNoChange, "choosing the already selected item should not report a change") &&
        Expect(closedIgnoresNavigation, "a closed model should ignore navigation and confirm") &&
        Expect(shrinkClearsSelection, "a selection past the new end should be cleared") &&
        Expect(setWithinRange && setOutOfRangeClears, "SetSelected should respect the range") &&
        Expect(emptyChoosesNothing, "an empty list should never produce a selection");
}

static const TestSupport::Registration gChoiceModelTests{
    "UIModel", "choice model tests should pass", RunChoiceModelTests };
