#include "PlayInputFocusTests.h"

#include <iostream>

#include "Rules/EditorPlayInputFocus.h"
#include "Platform/IInput.h"
#include "Runtime/Input.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>플레이 중이고 창이 앞에 있는, 아무 일도 없는 프레임이다.</summary>
    [[nodiscard]] GameEditor::PlayInputFocusFrame QuietPlayingFrame()
    {
        GameEditor::PlayInputFocusFrame frame;
        frame.isPlaying = true;
        frame.windowHasFocus = true;
        return frame;
    }
}

bool RunPlayInputFocusTests()
{
    using GameEditor::PlayInputFocusFrame;
    using GameEditor::PlayInputFocusResult;
    using GameEditor::ResolvePlayInputFocus;

    std::cout << "running play input focus tests\n";

    // 클릭 전에는 게임이 쥐지 않는다. 마우스가 뷰 위에 있다는 것만으로는 바뀌지 않는다는 것이
    // 이 규칙의 요점이다 — 커서가 스치는 것으로 단축키가 게임으로 새면 안 된다.
    bool passed = Expect(
        !ResolvePlayInputFocus(QuietPlayingFrame(), false).captured,
        "a quiet frame should not hand the input to the game");

    // 게임 뷰를 클릭하면 쥔다. 그 클릭은 게임에 전하지 않는다.
    PlayInputFocusFrame clicking = QuietPlayingFrame();
    clicking.pressedInsideGameView = true;
    const PlayInputFocusResult grabbed = ResolvePlayInputFocus(clicking, false);
    passed = Expect(grabbed.captured, "clicking the game view should hand it the input") && passed;
    passed = Expect(
        grabbed.swallowClick, "the click that grabs the focus should not reach the game") && passed;

    // 쥔 뒤에는 아무 일이 없어도 계속 쥔다. 상태이지 위치가 아니다.
    passed = Expect(
        ResolvePlayInputFocus(QuietPlayingFrame(), true).captured,
        "the game should keep the input until something takes it back") && passed;

    // Esc로 놓는다. 그 Esc는 게임이 보지 못한다 — 빠져나갈 길이 하나라도 확실해야 한다.
    PlayInputFocusFrame escaping = QuietPlayingFrame();
    escaping.escapePressed = true;
    passed = Expect(
        !ResolvePlayInputFocus(escaping, true).captured, "escape should give the input back") &&
        passed;

    // 플레이가 끝나면 놓는다.
    PlayInputFocusFrame stopped = QuietPlayingFrame();
    stopped.isPlaying = false;
    passed = Expect(
        !ResolvePlayInputFocus(stopped, true).captured, "leaving play should give the input back") &&
        passed;

    // 창이 뒤로 물러나도 놓는다. 잡힌 채 갇히는 상태를 만들지 않는다.
    PlayInputFocusFrame background = QuietPlayingFrame();
    background.windowHasFocus = false;
    passed = Expect(
        !ResolvePlayInputFocus(background, true).captured,
        "a window that went to the back should give the input back") && passed;

    // 편집 중에는 뷰를 클릭해도 쥐지 않는다. 쥘 게임이 돌고 있지 않다.
    PlayInputFocusFrame editing = QuietPlayingFrame();
    editing.isPlaying = false;
    editing.pressedInsideGameView = true;
    passed = Expect(
        !ResolvePlayInputFocus(editing, false).captured,
        "clicking the game view while editing should change nothing") && passed;

    // 쥐고 있는 동안의 클릭은 삼키지 않는다. 삼키는 것은 잡는 그 한 번뿐이다.
    PlayInputFocusFrame clickingAgain = QuietPlayingFrame();
    clickingAgain.pressedInsideGameView = true;
    passed = Expect(
        !ResolvePlayInputFocus(clickingAgain, true).swallowClick,
        "clicks after the first should reach the game") && passed;

    return passed;
}

bool RunPlayInputHandoffTests()
{
    using GameEngine::Platform::InputState;
    using GameEngine::Platform::Key;
    using GameEngine::Runtime::Input;

    std::cout << "running play input handoff tests\n";

    // 에디터가 받은 프레임이다: Space가 눌렸고 커서가 어딘가에 있다.
    InputState fromWindow;
    fromWindow.hasFocus = true;
    fromWindow.SetKey(Key::Space, true);
    fromWindow.cursor.x = 500;
    fromWindow.cursor.y = 300;

    // 쥐고 있을 때: 게임이 그 프레임을 받는다.
    Input gameInput;
    gameInput.BeginFrameWithState(fromWindow);
    bool passed = Expect(
        gameInput.GetKey(Key::Space) && gameInput.GetKeyDown(Key::Space),
        "the game should see the key that was pressed this frame");

    // 그리고 에디터는 그 프레임에 아무것도 받지 않는다 — 둘 다 받으면 단축키가 게임을 조작한다.
    Input editorInput;
    editorInput.BeginFrameWithState(fromWindow);
    editorInput.BeginFrameWithState({});
    passed = Expect(
        !editorInput.GetKey(Key::Space),
        "the editor should not hold a key it handed to the game") && passed;
    passed = Expect(
        !editorInput.GetKeyDown(Key::Space),
        "the editor should not see the press it handed to the game") && passed;

    // 쥐지 않을 때 빈 상태로 채우는 것과 채우지 않는 것은 다르다. 채우지 않으면 지난 프레임의
    // 눌림이 그대로 남아, 다음에 쥐었을 때 게임이 누르지도 않은 키를 붙들고 있다.
    Input released;
    released.BeginFrameWithState(fromWindow);
    passed = Expect(released.GetKey(Key::Space), "the game held the key while it had the input") &&
        passed;
    released.BeginFrameWithState({});
    std::cout << "  after handing the input back, the game holds Space: "
              << (released.GetKey(Key::Space) ? "yes" : "no") << ", sees it released: "
              << (released.GetKeyUp(Key::Space) ? "yes" : "no") << "\n";
    passed = Expect(
        !released.GetKey(Key::Space),
        "filling with an empty frame should release what the game was holding") && passed;

    return passed;
}

static const TestSupport::Registration gPlayInputFocusTests{
    "UIEvent", "play input focus tests should pass", RunPlayInputFocusTests };

static const TestSupport::Registration gPlayInputHandoffTests{
    "UIEvent", "play input handoff tests should pass", RunPlayInputHandoffTests };
