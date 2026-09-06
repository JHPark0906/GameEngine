#include "Rules/EditorMenuBarState.h"

#include "EditorMenuBarStateTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

bool RunEditorMenuBarStateTests()
{
    using GameEditor::EditorMenuBarState;

    // 처음에는 아무것도 펼쳐져 있지 않다.
    EditorMenuBarState state;
    const bool startsClosed = !state.IsOpen();

    // 머리줄을 누르면 열린다.
    state.PressHeading(0);
    const bool headingOpens = state.IsOpen() && state.GetOpenMenu() == 0;

    // 같은 머리줄을 다시 누르면 닫힌다. 이것이 없으면 한 번 연 메뉴를 닫으려고 사람이 엉뚱한
    // 곳을 눌러야 한다.
    state.PressHeading(0);
    const bool sameHeadingCloses = !state.IsOpen();

    // 다른 머리줄을 누르면 그쪽으로 옮겨 간다 — 닫았다 여는 두 동작이 아니라 한 동작이다.
    state.PressHeading(1);
    state.PressHeading(2);
    const bool otherHeadingMoves = state.IsOpen() && state.GetOpenMenu() == 2;

    // 밖을 누르면 닫힌다. 메뉴가 모달이 아니라는 것이 이 한 줄이다 — 모달이면 밖의 누름은
    // 아무 일도 하지 않는다.
    state.PressOutside();
    const bool outsideCloses = !state.IsOpen();

    // 닫혀 있을 때 밖을 눌러도 아무 일이 없다. 그 상태를 특별히 다루지 않아도 되어야, 부르는
    // 쪽이 "열려 있을 때만 알린다"는 조건을 들고 다니지 않는다.
    state.PressOutside();
    const bool outsideWhenClosedIsHarmless = !state.IsOpen();

    // 항목을 고르면 닫힌다. 명령이 실행되는데 목록이 남아 있으면 사람은 아직 실행되지 않았다고
    // 읽는다.
    state.PressHeading(1);
    state.ChooseItem();
    const bool choosingCloses = !state.IsOpen();

    // Esc는 아무것도 고르지 않고 닫는다.
    state.PressHeading(0);
    state.Cancel();
    const bool cancelCloses = !state.IsOpen();

    return Expect(startsClosed, "the menu bar should start with nothing open") &&
        Expect(headingOpens, "pressing a heading should open that menu") &&
        Expect(sameHeadingCloses, "pressing the open heading again should close it") &&
        Expect(otherHeadingMoves, "pressing another heading should move the open menu there") &&
        Expect(outsideCloses, "a press outside should close the menu — it is not modal") &&
        Expect(outsideWhenClosedIsHarmless, "a press outside with nothing open should do nothing") &&
        Expect(choosingCloses, "choosing an item should close the menu") &&
        Expect(cancelCloses, "cancelling should close the menu without choosing");
}

static const TestSupport::Registration gEditorMenuBarStateTests{
    "EditorDocument", "editor menu bar state tests should pass", RunEditorMenuBarStateTests };
