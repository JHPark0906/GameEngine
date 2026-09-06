#include <array>
#include <cmath>
#include <span>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../GameEngine/Assets/ResourceId.h"
#include "../GameEngine/Platform/IClipboard.h"
#include "../GameEngine/Platform/IInput.h"
#include "../GameEngine/Platform/ITextRasterizer.h"
#include "../GameEngine/Platform/PlatformServices.h"
#include "../GameEngine/Rendering/RenderFrame.h"
#include "../GameEngine/Rendering/TextRasterizationCache.h"
#include "../GameEngine/Rendering/RenderFrameBuilder.h"
#include "../GameEngine/Runtime/Input.h"
#include "../GameEngine/UI/UIContext.h"

#include "UIContextTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>
    /// 플랫폼 입력을 대신한다. UI 테스트는 커서를 놓고 버튼을 누르는 것으로 프레임을 연출한다.
    /// 읽기가 파괴적인 두 가지 — 타이핑된 텍스트와 휠 — 를 실제 플랫폼처럼 모델링한다.
    /// </summary>
    class FakeInput final : public GameEngine::Platform::IInput
    {
    public:
        void ReadState(GameEngine::Platform::InputState& state) override
        {
            state = mState;
            state.hasFocus = true;
            mState.TakeAccumulated();
        }

        void SetKey(const GameEngine::Platform::Key key, const bool isDown)
        {
            mState.SetKey(key, isDown);
        }

        void SetMouseButton(const GameEngine::Platform::MouseButton button, const bool isDown)
        {
            mState.SetMouseButton(button, isDown);
        }

        void SetCursor(const int x, const int y) { mState.cursor = { x, y }; }
        void Type(const std::string& text) { mState.typedText += text; }
        void Scroll(const float notches) { mState.wheelDelta += notches; }

    private:
        GameEngine::Platform::InputState mState;
    };

    /// <summary>
    /// 시스템 클립보드를 대신한다. 클립보드는 프로세스 밖의 전역 상태라서, 테스트는 진짜 대신
    /// 이것을 UIContext에 꽂고 내용을 직접 관찰한다.
    /// </summary>
    class FakeClipboard final : public GameEngine::Platform::IClipboard
    {
    public:
        [[nodiscard]] std::string GetText() override { return text; }
        void SetText(const std::string_view newText) override { text = std::string(newText); }

        std::string text;
    };

    /// <summary>
    /// 한 UI 프레임을 연출하는 무대이다. 입력을 진행시키고, UI 프레임을 열고, body가 위젯을
    /// 선언하게 한 뒤, 프레임을 닫아 만들어진 RenderFrame을 반환한다.
    /// </summary>
    template <typename Body>
    GameEngine::Rendering::RenderFrame RunUIFrame(
        FakeInput& source, GameEngine::Runtime::Input& input, GameEngine::UI::UIContext& ui,
        Body&& body)
    {
        input.BeginFrame(source);
        ui.BeginFrame(input, { 800, 600 });
        body();
        GameEngine::Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ 800, 600 });
        ui.EndFrame(builder);
        return std::move(builder).Build();
    }

    /// <summary>
    /// 버튼은 눌림과 뗌이 같은 위젯 위일 때만 발화한다. 눌러 놓고 밖으로 끌고 나가 떼는 것은
    /// 취소여야 한다 — 잘못 누른 것을 되돌리는 보편적인 몸짓이다.
    /// </summary>
    /// <summary>
    /// 유지 모드 UI가 가져간 포인터는 즉시 모드에 오지 않는다.
    ///
    /// 두 체계가 한 화면에 설 때 같은 클릭이 둘 다에게 가면, 툴바를 눌렀는데 그 아래 패널도
    /// 함께 반응한다. 반대로 잘못 가로채면 즉시 모드 UI 전체가 눌리지 않는데, 그쪽이 훨씬
    /// 나쁘다 — 그래서 판정은 "가져갔다"에 인색하고, 이 검사는 그 인색한 판정이 실제로 통할 때
    /// 무엇이 일어나는지를 고정한다.
    /// </summary>
    bool RunExternalPointerTests()
    {
        using namespace GameEngine;

        FakeInput source;
        Runtime::Input input;
        UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };
        const UI::WidgetId button = UI::MakeWidgetId("shared-button");
        const UI::UIRect rect{ 10.0f, 10.0f, 100.0f, 24.0f };

        // 다른 UI가 이 프레임의 포인터를 가져갔다. 버튼 위에서 누르고 떼어도 눌리지 않는다.
        ui.SetPointerConsumedExternally(true);
        source.SetCursor(50, 20);
        source.SetMouseButton(Platform::MouseButton::Left, true);
        bool fired = false;
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            fired = ui.DrawButton(button, rect, "Button");
        }));
        source.SetMouseButton(Platform::MouseButton::Left, false);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            fired = ui.DrawButton(button, rect, "Button") || fired;
        }));
        const bool consumedClickDoesNotFire = !fired;

        // 가져가지 않은 프레임에는 평소대로 눌린다 — 가로채기가 켜진 채로 남지 않는다.
        ui.SetPointerConsumedExternally(false);
        source.SetMouseButton(Platform::MouseButton::Left, true);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            fired = ui.DrawButton(button, rect, "Button");
        }));
        source.SetMouseButton(Platform::MouseButton::Left, false);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            fired = ui.DrawButton(button, rect, "Button");
        }));
        const bool releasedClickFires = fired;

        return Expect(
                consumedClickDoesNotFire,
                "a click taken by another UI should not reach an immediate-mode widget") &&
            Expect(
                releasedClickFires,
                "the immediate-mode UI should take clicks again once the pointer is released");
    }

    /// <summary>
    /// 다른 UI가 포인터를 소비해도 원시 포인터 값은 유지되어야 한다.
    /// 즉시 모드 위젯은 소비된 입력에 반응하지 않아야 하지만 유지 모드에서 시작한 드래그는
    /// 원시 입력을 읽어 커서 위치와 버튼 상태를 계속 추적해야 한다.
    /// </summary>
    bool RunRawPointerTests()
    {
        using namespace GameEngine;

        FakeInput source;
        Runtime::Input input;
        UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };

        source.SetCursor(320, 240);
        source.SetMouseButton(Platform::MouseButton::Left, true);

        // 다른 UI가 이 프레임의 포인터를 가져갔다.
        ui.SetPointerConsumedExternally(true);
        static_cast<void>(RunUIFrame(source, input, ui, [] {}));
        const bool widgetsSeeNothing = ui.GetMouseX() < -1000.0f && !ui.IsMouseDown();
        const bool gesturesSeeTheCursor = ui.GetPointerX() == 320.0f &&
            ui.GetPointerY() == 240.0f && ui.IsPointerDown();

        // 가져가지 않은 프레임에는 둘이 같은 것을 본다.
        ui.SetPointerConsumedExternally(false);
        static_cast<void>(RunUIFrame(source, input, ui, [] {}));
        const bool agreeWhenNotConsumed = ui.GetPointerX() == ui.GetMouseX() &&
            ui.GetPointerY() == ui.GetMouseY() && ui.IsPointerDown() == ui.IsMouseDown();

        // 버튼을 떼면 원시 값도 떼어진다 — 남는 값이 굳어 버리면 제스처가 끝나지 않는다.
        source.SetMouseButton(Platform::MouseButton::Left, false);
        ui.SetPointerConsumedExternally(true);
        static_cast<void>(RunUIFrame(source, input, ui, [] {}));
        const bool releaseReachesTheRawPointer = !ui.IsPointerDown();

        return Expect(
                widgetsSeeNothing,
                "a consumed frame should hide the cursor and the button from widgets") &&
            Expect(
                gesturesSeeTheCursor,
                "a consumed frame should still report the real pointer to retained-mode gestures") &&
            Expect(agreeWhenNotConsumed, "an unconsumed frame should report one pointer, not two") &&
            Expect(
                releaseReachesTheRawPointer,
                "releasing the button should reach the raw pointer so a gesture can end");
    }

    bool RunButtonTests()
    {
        using namespace GameEngine;

        FakeInput source;
        Runtime::Input input;
        UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };
        const UI::WidgetId button = UI::MakeWidgetId("button");
        const UI::UIRect rect{ 10.0f, 10.0f, 100.0f, 24.0f };

        source.SetCursor(50, 20);
        bool fired = false;
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            fired = ui.DrawButton(button, rect, "Button");
        }));
        const bool hoverAloneDoesNotFire = !fired;

        source.SetMouseButton(Platform::MouseButton::Left, true);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            fired = ui.DrawButton(button, rect, "Button");
        }));
        const bool pressAloneDoesNotFire = !fired;

        source.SetMouseButton(Platform::MouseButton::Left, false);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            fired = ui.DrawButton(button, rect, "Button");
        }));
        const bool releaseOnWidgetFires = fired;

        // 버튼 위에서 누르고, 밖으로 나가서 뗀다.
        source.SetMouseButton(Platform::MouseButton::Left, true);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            fired = ui.DrawButton(button, rect, "Button");
        }));
        source.SetCursor(400, 400);
        source.SetMouseButton(Platform::MouseButton::Left, false);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            fired = ui.DrawButton(button, rect, "Button");
        }));
        const bool draggingOutCancels = !fired;

        return Expect(hoverAloneDoesNotFire, "hovering a button should not fire it") &&
            Expect(pressAloneDoesNotFire, "pressing a button should not fire until release") &&
            Expect(releaseOnWidgetFires, "releasing over the pressed button should fire it") &&
            Expect(draggingOutCancels, "releasing outside the pressed button should cancel");
    }

    /// <summary>
    /// 텍스트 필드는 클릭으로 포커스를 얻고, 타이핑을 끝에 붙이고, 백스페이스로 UTF-8 문자
    /// 하나를 — 바이트 하나가 아니라 — 지운다. 빈 곳 클릭이 포커스를 거둔다.
    /// </summary>
    bool RunTextFieldTests()
    {
        using namespace GameEngine;

        FakeInput source;
        Runtime::Input input;
        UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };
        const UI::WidgetId field = UI::MakeWidgetId("field");
        const UI::UIRect rect{ 10.0f, 10.0f, 160.0f, 22.0f };
        std::string text;

        // 포커스가 없는 동안의 타이핑은 필드에 닿지 않아야 한다.
        source.SetCursor(400, 400);
        source.Type("stray");
        bool changed = false;
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            changed = ui.DrawTextField(field, rect, text);
        }));
        const bool unfocusedIgnoresTyping = !changed && text.empty();

        // 클릭해 포커스를 얻는다. 커서를 얹은 프레임과 누르는 프레임이 다른 것은 사람이 마우스를
        // 움직여 필드에 닿는 모습 그대로다 — 포인터의 임자는 직전 프레임의 선언들로 정해진다.
        source.SetCursor(50, 20);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            changed = ui.DrawTextField(field, rect, text);
        }));

        source.SetMouseButton(Platform::MouseButton::Left, true);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            changed = ui.DrawTextField(field, rect, text);
        }));
        const bool clickFocuses = ui.IsAnyTextFieldFocused();

        source.SetMouseButton(Platform::MouseButton::Left, false);
        source.Type("ab\xEA\xB0\x80"); // "ab가"
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            changed = ui.DrawTextField(field, rect, text);
        }));
        const bool typingAppends = changed && text == "ab\xEA\xB0\x80";

        // 백스페이스 한 번이 3바이트짜리 "가"를 통째로 지운다.
        source.SetKey(Platform::Key::Backspace, true);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            changed = ui.DrawTextField(field, rect, text);
        }));
        source.SetKey(Platform::Key::Backspace, false);
        const bool backspaceDeletesOneCharacter = changed && text == "ab";

        // 필드 밖을 클릭하면 포커스가 걷힌다.
        source.SetCursor(400, 400);
        source.SetMouseButton(Platform::MouseButton::Left, true);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            changed = ui.DrawTextField(field, rect, text);
        }));
        const bool clickingElsewhereUnfocuses = !ui.IsAnyTextFieldFocused();

        // 필드를 다시 포커스한 뒤, 다음 프레임에 필드가 선언되지 않으면 — 스크롤로 컬링됐거나
        // 패널이 닫혔으면 — 포커스가 걷힌다. 보이지 않는 필드가 포커스를 쥐면 타이핑은 어디에도
        // 닿지 않으면서 단축키만 삼킨다.
        source.SetMouseButton(Platform::MouseButton::Left, false);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            changed = ui.DrawTextField(field, rect, text);
        }));
        source.SetCursor(50, 20);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            changed = ui.DrawTextField(field, rect, text);
        }));
        source.SetMouseButton(Platform::MouseButton::Left, true);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            changed = ui.DrawTextField(field, rect, text);
        }));
        source.SetMouseButton(Platform::MouseButton::Left, false);
        const bool refocused = ui.IsAnyTextFieldFocused();
        static_cast<void>(RunUIFrame(source, input, ui, [&] {}));
        const bool culledFieldLosesFocus = !ui.IsAnyTextFieldFocused();

        return Expect(unfocusedIgnoresTyping, "an unfocused field should ignore typed text") &&
            Expect(clickFocuses, "clicking a field should focus it") &&
            Expect(typingAppends, "typing into a focused field should append") &&
            Expect(
                backspaceDeletesOneCharacter,
                "backspace should delete one UTF-8 character, not one byte") &&
            Expect(clickingElsewhereUnfocuses, "clicking empty space should clear focus") &&
            Expect(refocused, "clicking the field again should refocus it") &&
            Expect(
                culledFieldLosesFocus,
                "a focused field that is not drawn this frame should lose focus");
    }

    /// <summary>
    /// 텍스트 필드의 무대이다: 필드 하나를 클릭해 포커스를 준 상태로 시작하고, 키 하나를
    /// 누르는 프레임 — 누르고, 한 프레임 돌리고, 뗀다 — 을 줄여 쓴다.
    /// </summary>
    struct TextFieldStage
    {
        /// <summary>
        /// 클립보드를 쓰는 시험은 가짜를 건네고, 나머지는 클립보드 없이 선다. UI가 스스로
        /// 플랫폼 클립보드를 만들면 만들어진 것을 나중에 갈아끼워야 한다.
        /// </summary>
        explicit TextFieldStage(
            std::unique_ptr<GameEngine::Platform::IClipboard> clipboard = nullptr)
            : ui(
                  std::move(clipboard),
                  TestSupport::CreateTestTextRasterizer())
        {
        }

        FakeInput source;
        GameEngine::Runtime::Input input;
        GameEngine::UI::UIContext ui;
        GameEngine::UI::WidgetId field = GameEngine::UI::MakeWidgetId("field");
        GameEngine::UI::UIRect rect{ 10.0f, 10.0f, 160.0f, 22.0f };
        std::string text;
        bool changed = false;

        void Frame()
        {
            static_cast<void>(RunUIFrame(source, input, ui, [&] {
                changed = ui.DrawTextField(field, rect, text);
            }));
        }

        /// <summary>
        /// 필드 왼쪽 끝을 클릭해 포커스를 준다. 빈 필드에서는 캐럿이 0이다.
        ///
        /// 커서를 얹고 한 프레임을 흘린 뒤에 누른다. 포인터의 임자는 직전 프레임의 선언들로
        /// 정해지므로 위젯이 커서 아래 처음 나타난 프레임에는 아직 임자가 아니고, 이 순서가
        /// 사람이 마우스를 움직여 필드를 누르는 모습이기도 하다.
        /// </summary>
        void FocusByClick()
        {
            source.SetCursor(16, 20);
            Frame();
            source.SetMouseButton(GameEngine::Platform::MouseButton::Left, true);
            Frame();
            source.SetMouseButton(GameEngine::Platform::MouseButton::Left, false);
            Frame();
        }

        void Press(const GameEngine::Platform::Key key)
        {
            source.SetKey(key, true);
            Frame();
            source.SetKey(key, false);
            // 뗌도 한 프레임 보여야 한다: 눌림은 두 상태의 비교라서, 뗌을 건너뛰면 같은 키의
            // 다음 눌림이 눌림으로 읽히지 않는다.
            Frame();
        }

        [[nodiscard]] std::size_t Caret() const { return ui.GetTextEditState().caret; }
        [[nodiscard]] std::size_t SelectionBegin() const
        {
            return ui.GetTextEditState().selectionBegin;
        }
        [[nodiscard]] std::size_t SelectionEnd() const { return ui.GetTextEditState().selectionEnd; }
    };

    /// <summary>
    /// 캐럿은 방향키로 UTF-8 문자 단위로 움직이고, Home/End로 양끝에 가며, 입력·삭제는 끝이
    /// 아니라 캐럿 자리에서 일어난다.
    /// </summary>
    bool RunTextEditCaretTests()
    {
        using namespace GameEngine;

        TextFieldStage stage;
        stage.FocusByClick();
        stage.source.Type("abc");
        stage.Frame();
        const bool typingLeavesCaretAtEnd = stage.text == "abc" && stage.Caret() == 3;

        stage.Press(Platform::Key::Left);
        stage.Press(Platform::Key::Left);
        const bool leftMovesCaret = stage.Caret() == 1;

        stage.source.Type("X");
        stage.Frame();
        const bool insertHappensAtCaret = stage.text == "aXbc" && stage.Caret() == 2;

        stage.Press(Platform::Key::Home);
        const bool homeGoesToStart = stage.Caret() == 0;
        stage.source.Type("Y");
        stage.Frame();
        const bool insertAtStartWorks = stage.text == "YaXbc" && stage.Caret() == 1;

        stage.Press(Platform::Key::End);
        const bool endGoesToEnd = stage.Caret() == 5;
        stage.Press(Platform::Key::Backspace);
        const bool backspaceDeletesBeforeCaret = stage.text == "YaXb";

        stage.Press(Platform::Key::Home);
        stage.Press(Platform::Key::Delete);
        const bool deleteRemovesAfterCaret = stage.text == "aXb" && stage.Caret() == 0;

        // UTF-8: "가"는 3바이트다. 방향키와 Delete가 문자 하나로 다뤄야 한다.
        stage.Press(Platform::Key::End);
        stage.source.Type("\xEA\xB0\x80"); // "가"
        stage.Frame();
        const bool multibyteInsert = stage.text == "aXb\xEA\xB0\x80" && stage.Caret() == 6;
        stage.Press(Platform::Key::Left);
        const bool leftCrossesMultibyte = stage.Caret() == 3;
        stage.Press(Platform::Key::Delete);
        const bool deleteRemovesMultibyte = stage.text == "aXb";

        stage.Press(Platform::Key::Enter);
        const bool enterUnfocuses = !stage.ui.IsAnyTextFieldFocused();

        return Expect(typingLeavesCaretAtEnd, "typing should leave the caret after the text") &&
            Expect(leftMovesCaret, "left should move the caret one character") &&
            Expect(insertHappensAtCaret, "typing should insert at the caret") &&
            Expect(homeGoesToStart, "home should move the caret to the start") &&
            Expect(insertAtStartWorks, "typing at the start should prepend") &&
            Expect(endGoesToEnd, "end should move the caret to the end") &&
            Expect(backspaceDeletesBeforeCaret, "backspace should delete before the caret") &&
            Expect(deleteRemovesAfterCaret, "delete should remove the character after the caret") &&
            Expect(multibyteInsert, "a multibyte character should insert as one character") &&
            Expect(leftCrossesMultibyte, "left should cross a multibyte character in one step") &&
            Expect(deleteRemovesMultibyte, "delete should remove a whole multibyte character") &&
            Expect(enterUnfocuses, "enter should clear field focus");
    }

    /// <summary>
    /// 선택은 Shift+이동으로 자라고, 입력·삭제가 선택을 통째로 대체하며, 방향키 단독은 선택을
    /// 그 방향의 끝으로 접는다.
    /// </summary>
    bool RunTextSelectionTests()
    {
        using namespace GameEngine;

        TextFieldStage stage;
        stage.FocusByClick();
        stage.source.Type("hello");
        stage.Frame();

        stage.source.SetKey(Platform::Key::Shift, true);
        stage.Press(Platform::Key::Left);
        stage.Press(Platform::Key::Left);
        stage.source.SetKey(Platform::Key::Shift, false);
        const bool shiftLeftSelects =
            stage.SelectionBegin() == 3 && stage.SelectionEnd() == 5 && stage.Caret() == 3;

        stage.source.Type("X");
        stage.Frame();
        const bool typingReplacesSelection = stage.text == "helX" && stage.Caret() == 4;

        stage.source.SetKey(Platform::Key::Control, true);
        stage.Press(Platform::Key::A);
        stage.source.SetKey(Platform::Key::Control, false);
        const bool selectAllSpansText =
            stage.SelectionBegin() == 0 && stage.SelectionEnd() == 4;

        stage.Press(Platform::Key::Left);
        const bool plainLeftCollapsesToBegin =
            stage.Caret() == 0 && stage.SelectionBegin() == stage.SelectionEnd();

        stage.source.SetKey(Platform::Key::Shift, true);
        stage.Press(Platform::Key::End);
        stage.source.SetKey(Platform::Key::Shift, false);
        const bool shiftEndSelectsToEnd =
            stage.SelectionBegin() == 0 && stage.SelectionEnd() == 4 && stage.Caret() == 4;

        stage.Press(Platform::Key::Backspace);
        const bool backspaceErasesSelection = stage.text.empty() && stage.Caret() == 0;

        return Expect(shiftLeftSelects, "shift+left should grow a selection") &&
            Expect(typingReplacesSelection, "typing should replace the selection") &&
            Expect(selectAllSpansText, "ctrl+a should select the whole text") &&
            Expect(plainLeftCollapsesToBegin, "left should collapse a selection to its start") &&
            Expect(shiftEndSelectsToEnd, "shift+end should select to the end") &&
            Expect(backspaceErasesSelection, "backspace should erase the whole selection");
    }

    /// <summary>
    /// Ctrl+C/X/V는 꽂힌 클립보드를 오간다. 붙여넣기의 개행은 걸러진다 — 필드는 한 줄이다.
    /// </summary>
    bool RunTextClipboardTests()
    {
        using namespace GameEngine;

        auto clipboardOwned = std::make_unique<FakeClipboard>();
        FakeClipboard* const clipboard = clipboardOwned.get();
        TextFieldStage stage(std::move(clipboardOwned));

        stage.FocusByClick();
        stage.source.Type("abcdef");
        stage.Frame();

        stage.Press(Platform::Key::Home);
        stage.source.SetKey(Platform::Key::Shift, true);
        stage.Press(Platform::Key::Right);
        stage.Press(Platform::Key::Right);
        stage.source.SetKey(Platform::Key::Shift, false);

        stage.source.SetKey(Platform::Key::Control, true);
        stage.Press(Platform::Key::C);
        stage.source.SetKey(Platform::Key::Control, false);
        const bool copyTakesSelection = clipboard->text == "ab" && stage.text == "abcdef";

        stage.Press(Platform::Key::End);
        stage.source.SetKey(Platform::Key::Control, true);
        stage.Press(Platform::Key::V);
        stage.source.SetKey(Platform::Key::Control, false);
        const bool pasteInsertsAtCaret = stage.text == "abcdefab" && stage.Caret() == 8;

        stage.source.SetKey(Platform::Key::Control, true);
        stage.Press(Platform::Key::A);
        stage.Press(Platform::Key::X);
        stage.source.SetKey(Platform::Key::Control, false);
        const bool cutTakesAndErases = clipboard->text == "abcdefab" && stage.text.empty();

        clipboard->text = "one\r\ntwo\tend";
        stage.source.SetKey(Platform::Key::Control, true);
        stage.Press(Platform::Key::V);
        stage.source.SetKey(Platform::Key::Control, false);
        const bool pasteFiltersControlCharacters = stage.text == "onetwoend";

        return Expect(copyTakesSelection, "ctrl+c should copy the selection unchanged") &&
            Expect(pasteInsertsAtCaret, "ctrl+v should insert the clipboard at the caret") &&
            Expect(cutTakesAndErases, "ctrl+x should copy and erase the selection") &&
            Expect(
                pasteFiltersControlCharacters,
                "pasting should drop newlines and control characters");
    }

    /// <summary>
    /// 클릭은 캐럿을 가장 가까운 문자 경계에 놓고, 클릭-드래그는 앵커를 남겨 선택을 만든다.
    /// 픽셀 좌표는 글꼴에 따라 다르므로, 글꼴과 무관한 양끝 — 텍스트보다 왼쪽과 오른쪽 — 만
    /// 고정한다.
    /// </summary>
    bool RunTextClickCaretTests()
    {
        using namespace GameEngine;

        TextFieldStage stage;
        stage.FocusByClick();
        stage.source.Type("abcdef");
        stage.Frame();

        // 텍스트 오른쪽 끝 너머를 클릭하면 캐럿은 끝이다.
        stage.source.SetCursor(165, 20);
        stage.source.SetMouseButton(Platform::MouseButton::Left, true);
        stage.Frame();
        stage.source.SetMouseButton(Platform::MouseButton::Left, false);
        stage.Frame();
        const bool clickPastEndPlacesCaretAtEnd =
            stage.Caret() == 6 && stage.SelectionBegin() == stage.SelectionEnd();

        // 텍스트 시작보다 왼쪽을 클릭하면 캐럿은 0이다.
        stage.source.SetCursor(12, 20);
        stage.source.SetMouseButton(Platform::MouseButton::Left, true);
        stage.Frame();
        stage.source.SetMouseButton(Platform::MouseButton::Left, false);
        stage.Frame();
        const bool clickBeforeStartPlacesCaretAtStart = stage.Caret() == 0;

        // 오른쪽 끝에서 눌러 왼쪽 끝까지 끌면 전체가 선택된다.
        stage.source.SetCursor(165, 20);
        stage.source.SetMouseButton(Platform::MouseButton::Left, true);
        stage.Frame();
        stage.source.SetCursor(12, 20);
        stage.Frame();
        stage.source.SetMouseButton(Platform::MouseButton::Left, false);
        stage.Frame();
        const bool dragSelectsRange = stage.SelectionBegin() == 0 && stage.SelectionEnd() == 6 &&
            stage.Caret() == 0;

        return Expect(clickPastEndPlacesCaretAtEnd, "a click past the text should park the caret at the end") &&
            Expect(
                clickBeforeStartPlacesCaretAtStart,
                "a click before the text should park the caret at the start") &&
            Expect(dragSelectsRange, "dragging across the field should select the crossed range");
    }

    /// <summary>
    /// 선택 행의 더블클릭은 같은 행 위의 짧은 간격 두 번째 클릭이다. 첫 클릭은 클릭이기만 하다.
    /// </summary>
    /// <summary>
    /// 선언 순서가 유일한 순서다: 커서 아래 마지막으로 선언된 위젯이 포인터를 갖는다.
    ///
    /// 그리는 순서도 선언 순서라 나중에 선언된 것이 위에 얹히므로, 이 규칙은 "눈에 보이는 맨
    /// 위의 것이 입력을 받는다"와 같은 말이다. 겹치는 위젯이 없는 현재 배치에서는 커서 아래
    /// 것이 언제나 하나뿐이라 규칙이 있으나 없으나 답이 같다 — 그래서 이 검사는 배치가 아니라
    /// 규칙을 건다. 겹침은 여기서 만들어 낸다.
    /// </summary>
    bool RunHoverOrderTests()
    {
        using namespace GameEngine;

        const UI::WidgetId first = UI::MakeWidgetId("first");
        const UI::WidgetId second = UI::MakeWidgetId("second");

        // 겹치지 않는 두 위젯에서는 커서 아래 위젯만 반응한다.
        bool separateFirstFires = false;
        bool separateSecondStaysQuiet = false;
        {
            FakeInput source;
            Runtime::Input input;
            UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };
            const UI::UIRect left{ 0.0f, 0.0f, 100.0f, 20.0f };
            const UI::UIRect right{ 200.0f, 0.0f, 100.0f, 20.0f };
            bool leftFired = false;
            bool rightFired = false;
            const auto declareBoth = [&] {
                leftFired = ui.DrawButton(first, left, "Left") || leftFired;
                rightFired = ui.DrawButton(second, right, "Right") || rightFired;
            };

            source.SetCursor(50, 10);
            static_cast<void>(RunUIFrame(source, input, ui, declareBoth));
            source.SetMouseButton(Platform::MouseButton::Left, true);
            static_cast<void>(RunUIFrame(source, input, ui, declareBoth));
            source.SetMouseButton(Platform::MouseButton::Left, false);
            static_cast<void>(RunUIFrame(source, input, ui, declareBoth));
            separateFirstFires = leftFired;
            separateSecondStaysQuiet = !rightFired;
        }

        // 같은 자리에 포개진 둘: 나중에 선언된 것 — 위에 그려진 것 — 만 반응한다.
        bool overlappedLaterFires = false;
        bool overlappedEarlierStaysQuiet = false;
        {
            FakeInput source;
            Runtime::Input input;
            UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };
            const UI::UIRect shared{ 0.0f, 0.0f, 100.0f, 20.0f };
            bool underFired = false;
            bool overFired = false;
            const auto declareStacked = [&] {
                underFired = ui.DrawButton(first, shared, "Under") || underFired;
                overFired = ui.DrawButton(second, shared, "Over") || overFired;
            };

            source.SetCursor(50, 10);
            static_cast<void>(RunUIFrame(source, input, ui, declareStacked));
            source.SetMouseButton(Platform::MouseButton::Left, true);
            static_cast<void>(RunUIFrame(source, input, ui, declareStacked));
            source.SetMouseButton(Platform::MouseButton::Left, false);
            static_cast<void>(RunUIFrame(source, input, ui, declareStacked));
            overlappedLaterFires = overFired;
            overlappedEarlierStaysQuiet = !underFired;
        }

        // 선택 항목이 겹쳐도 가려진 항목은 같은 누름에 함께 선택되지 않아야 한다.
        bool selectableOverlapPicksOne = false;
        {
            FakeInput source;
            Runtime::Input input;
            UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };
            const UI::UIRect shared{ 0.0f, 0.0f, 200.0f, 20.0f };
            UI::UIContext::SelectableResult under;
            UI::UIContext::SelectableResult over;
            const auto declareStacked = [&] {
                under = ui.DrawSelectable(first, shared, "Under", false);
                over = ui.DrawSelectable(second, shared, "Over", false);
            };

            source.SetCursor(50, 10);
            static_cast<void>(RunUIFrame(source, input, ui, declareStacked));
            source.SetMouseButton(Platform::MouseButton::Left, true);
            static_cast<void>(RunUIFrame(source, input, ui, declareStacked));
            selectableOverlapPicksOne = over.clicked && !under.clicked;
        }

        // 겹친 목록에서도 한 번의 휠 입력은 포인터를 소유한 목록 하나만 움직인다.
        bool scrollOverlapMovesOne = false;
        {
            FakeInput source;
            Runtime::Input input;
            UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };
            const UI::UIRect shared{ 0.0f, 0.0f, 200.0f, 100.0f };
            float underOffset = 0.0f;
            float overOffset = 0.0f;
            const auto declareStacked = [&] {
                underOffset = ui.ApplyScroll(first, shared, 500.0f);
                overOffset = ui.ApplyScroll(second, shared, 500.0f);
            };

            source.SetCursor(100, 50);
            static_cast<void>(RunUIFrame(source, input, ui, declareStacked));
            source.Scroll(-1.0f);
            static_cast<void>(RunUIFrame(source, input, ui, declareStacked));
            scrollOverlapMovesOne = overOffset > 0.0f && underOffset == 0.0f;
        }

        // 임자는 직전 프레임의 선언들로 정해진다. 그래서 위젯이 커서 아래 처음 나타난 프레임에는
        // 아직 임자가 아니다 — 즉시 모드에서 이 지연 없이 z를 얻을 방법은 없으므로, 감추는 대신
        // 성질로 못박는다. 커서가 움직여 위젯에 닿는 보통의 경우에는 닿는 프레임과 누르는
        // 프레임이 달라서 드러나지 않고, 정지한 커서 아래로 위젯이 나타났을 때만 한 프레임이다.
        bool firstFrameIsNotYetHovered = false;
        bool nextAttemptFires = false;
        {
            FakeInput source;
            Runtime::Input input;
            UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };
            const UI::UIRect rect{ 0.0f, 0.0f, 100.0f, 20.0f };
            bool fired = false;
            const auto declare = [&] { fired = ui.DrawButton(first, rect, "Button") || fired; };

            // 위젯이 나타나는 바로 그 프레임에 누른다.
            source.SetCursor(50, 10);
            source.SetMouseButton(Platform::MouseButton::Left, true);
            static_cast<void>(RunUIFrame(source, input, ui, declare));
            source.SetMouseButton(Platform::MouseButton::Left, false);
            static_cast<void>(RunUIFrame(source, input, ui, declare));
            firstFrameIsNotYetHovered = !fired;

            // 그다음 누름부터는 평소대로 발화한다.
            source.SetMouseButton(Platform::MouseButton::Left, true);
            static_cast<void>(RunUIFrame(source, input, ui, declare));
            source.SetMouseButton(Platform::MouseButton::Left, false);
            static_cast<void>(RunUIFrame(source, input, ui, declare));
            nextAttemptFires = fired;
        }

        return Expect(
                separateFirstFires && separateSecondStaysQuiet,
                "widgets that do not overlap should react exactly as they did before") &&
            Expect(
                overlappedLaterFires && overlappedEarlierStaysQuiet,
                "of two stacked buttons only the later declared one should take the click") &&
            Expect(
                selectableOverlapPicksOne,
                "of two stacked selectables only the later declared one should be picked") &&
            Expect(
                scrollOverlapMovesOne,
                "of two stacked scroll regions only the later declared one should scroll") &&
            Expect(
                firstFrameIsNotYetHovered,
                "a widget appearing under the cursor should not own the pointer that frame") &&
            Expect(nextAttemptFires, "it should own the pointer from the following frame");
    }

    bool RunSelectableTests()
    {
        using namespace GameEngine;

        FakeInput source;
        Runtime::Input input;
        UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };
        const UI::WidgetId row = UI::MakeWidgetId("row", 3);
        const UI::UIRect rect{ 0.0f, 40.0f, 200.0f, 20.0f };

        // 커서를 먼저 얹고 한 프레임을 흘린다. 포인터의 임자는 직전 프레임의 선언들로 정해지므로
        // 위젯이 커서 아래 처음 나타난 프레임에는 아직 임자가 아니다.
        source.SetCursor(20, 50);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            static_cast<void>(ui.DrawSelectable(row, rect, "Row", false));
        }));

        source.SetMouseButton(Platform::MouseButton::Left, true);
        UI::UIContext::SelectableResult result;
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            result = ui.DrawSelectable(row, rect, "Row", false);
        }));
        const bool firstClickIsSingle = result.clicked && !result.doubleClicked;

        source.SetMouseButton(Platform::MouseButton::Left, false);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            result = ui.DrawSelectable(row, rect, "Row", true);
        }));

        // 테스트는 실제 시계로 돈다: 연이은 두 프레임은 더블클릭 간격 안에 넉넉히 든다.
        source.SetMouseButton(Platform::MouseButton::Left, true);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            result = ui.DrawSelectable(row, rect, "Row", true);
        }));
        const bool secondClickIsDouble = result.clicked && result.doubleClicked;

        return Expect(firstClickIsSingle, "the first click should not read as a double click") &&
            Expect(secondClickIsDouble, "a quick second click should read as a double click");
    }

    /// <summary>
    /// 스크롤 오프셋은 영역 위의 휠로 자라고, 내용 높이를 넘지 못하며, 내용이 다 보이면 0이다.
    /// </summary>
    bool RunScrollTests()
    {
        using namespace GameEngine;

        FakeInput source;
        Runtime::Input input;
        UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };
        const UI::WidgetId list = UI::MakeWidgetId("list");
        const UI::UIRect rect{ 0.0f, 0.0f, 200.0f, 100.0f };

        // 커서를 먼저 얹고 한 프레임을 흘린다 — 휠도 포인터의 임자에게만 간다.
        source.SetCursor(100, 50);
        float offset = 0.0f;
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            offset = ui.ApplyScroll(list, rect, 500.0f);
        }));

        source.Scroll(-1.0f);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            offset = ui.ApplyScroll(list, rect, 500.0f);
        }));
        const bool wheelScrollsDown = offset > 0.0f;

        // 아주 큰 스크롤도 내용의 끝을 넘지 못한다.
        source.Scroll(-100.0f);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            offset = ui.ApplyScroll(list, rect, 500.0f);
        }));
        const bool clampedToContent = offset == 400.0f;

        // 내용이 영역에 다 들어가면 스크롤할 것이 없다.
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            offset = ui.ApplyScroll(list, rect, 50.0f);
        }));
        const bool shortContentDoesNotScroll = offset == 0.0f;

        return Expect(wheelScrollsDown, "wheel over the list should scroll it") &&
            Expect(clampedToContent, "scrolling should stop at the end of the content") &&
            Expect(shortContentDoesNotScroll, "content that fits should not scroll");
    }

    /// <summary>
    /// 상호작용 이미지의 드래그는 그 위에서 시작됐을 때만 그 위젯의 것이고, 밖으로 나가도 버튼을
    /// 쥔 동안은 계속된다 — 궤도 회전 중에 커서가 뷰를 벗어나는 일은 흔하다.
    /// </summary>
    bool RunInteractiveImageTests()
    {
        using namespace GameEngine;

        FakeInput source;
        Runtime::Input input;
        UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };
        const UI::WidgetId view = UI::MakeWidgetId("scene-view");
        const UI::UIRect rect{ 100.0f, 100.0f, 400.0f, 300.0f };

        // 커서를 뷰에 얹고 한 프레임을 흘린 뒤 누른다 — 포인터의 임자는 직전 프레임의 선언들로
        // 정해지므로, 위젯이 커서 아래 처음 나타난 프레임에는 아직 임자가 아니다.
        source.SetCursor(200, 200);
        UI::ImageInteraction interaction;
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            interaction = ui.DrawInteractiveImage(view, rect, nullptr);
        }));

        source.SetMouseButton(Platform::MouseButton::Left, true);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            interaction = ui.DrawInteractiveImage(view, rect, nullptr);
        }));
        const bool pressStartsDrag = interaction.hovered && interaction.leftDragging;

        // 커서가 뷰 밖으로 나가도 드래그는 계속되고, 이동량이 보고된다.
        source.SetCursor(700, 50);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            interaction = ui.DrawInteractiveImage(view, rect, nullptr);
        }));
        const bool dragSurvivesLeaving = !interaction.hovered && interaction.leftDragging &&
            interaction.dragDeltaX == 500.0f && interaction.dragDeltaY == -150.0f;

        source.SetMouseButton(Platform::MouseButton::Left, false);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            interaction = ui.DrawInteractiveImage(view, rect, nullptr);
        }));
        const bool releaseEndsDrag = !interaction.leftDragging;

        // 뷰 밖에서 시작된 드래그는 이 위젯의 것이 아니다.
        source.SetCursor(50, 50);
        source.SetMouseButton(Platform::MouseButton::Left, true);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            interaction = ui.DrawInteractiveImage(view, rect, nullptr);
        }));
        source.SetCursor(200, 200);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            interaction = ui.DrawInteractiveImage(view, rect, nullptr);
        }));
        const bool outsideStartIsNotADrag = !interaction.leftDragging;

        return Expect(pressStartsDrag, "pressing on the image should start a drag") &&
            Expect(dragSurvivesLeaving, "a drag should continue outside the image") &&
            Expect(releaseEndsDrag, "releasing the button should end the drag") &&
            Expect(outsideStartIsNotADrag, "a drag started outside should not belong to the image");
    }

    /// <summary>
    /// 활성 위젯은 자기를 잡은 버튼의 것이다. 가운데 드래그(팬)가 끝난 뒤 빈 곳을 왼쪽으로
    /// 누르면, 그 눌림은 이미지의 왼쪽 드래그(궤도)로 읽혀서는 안 된다.
    /// </summary>
    bool RunButtonOwnershipTests()
    {
        using namespace GameEngine;

        FakeInput source;
        Runtime::Input input;
        UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };
        const UI::WidgetId view = UI::MakeWidgetId("scene-view");
        const UI::UIRect rect{ 100.0f, 100.0f, 400.0f, 300.0f };

        // 가운데 버튼으로 잡고 끈다. 커서를 얹은 프레임과 누르는 프레임이 다른 것은 포인터의
        // 임자가 직전 프레임의 선언들로 정해지기 때문이다.
        source.SetCursor(200, 200);
        UI::ImageInteraction interaction;
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            interaction = ui.DrawInteractiveImage(view, rect, nullptr);
        }));

        source.SetMouseButton(Platform::MouseButton::Middle, true);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            interaction = ui.DrawInteractiveImage(view, rect, nullptr);
        }));
        const bool middleStartsPan = interaction.middleDragging && !interaction.leftDragging;

        source.SetMouseButton(Platform::MouseButton::Middle, false);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            interaction = ui.DrawInteractiveImage(view, rect, nullptr);
        }));

        // 빈 곳에서 왼쪽 버튼을 누른다. 이미지는 이것을 자기 드래그로 보면 안 된다.
        source.SetCursor(700, 50);
        source.SetMouseButton(Platform::MouseButton::Left, true);
        static_cast<void>(RunUIFrame(source, input, ui, [&] {
            interaction = ui.DrawInteractiveImage(view, rect, nullptr);
        }));
        const bool staleOwnershipCleared = !interaction.leftDragging && !interaction.middleDragging;

        return Expect(middleStartsPan, "a middle press on the image should start a middle drag") &&
            Expect(staleOwnershipCleared, "a later left press elsewhere should not read as the image's left drag");
    }

    /// <summary>
    /// 역할별 폰트 배정의 계약이다: 읽을 수 없는 바이트는 배정을 거절하고, 배정되지 않은
    /// 역할은 기본 UI 폰트로 그려진다 — 폰트 파일이 없다고 에디터가 글자를 잃지는 않는다.
    /// 실제 폰트 파일로의 배정은 시스템 폰트 파일로 확인한다.
    /// </summary>
    bool RunFontRoleTests()
    {
        using namespace GameEngine;

        FakeInput source;
        Runtime::Input input;
        // TestFont가 먼저 등록된 채로 시작한다 — 실제 에디터에서 제목·본문·고정폭 세 파일 중
        // 하나만 못 읽었을 때와 같은 모양이다: 그 역할만 배정이 없고, 다른 역할이 등록한 폰트로
        // 대신 그려진다. 아무 폰트도 없이 시작하면 "배정 없음"과 "폰트 자체가 없음"이 같아져
        // 이 시험이 확인하려는 대체 자체가 일어나지 않는다.
        auto rasterizer = TestSupport::CreateTestTextRasterizer();
        if (!rasterizer)
        {
            std::cout << "  UI font role tests skipped: no bundled font on this machine\n";
            return true;
        }
        UI::UIContext ui{ nullptr, std::move(rasterizer) };

        // 폰트가 아닌 바이트는 거절된다.
        const std::array<std::byte, 8> notAFont{};
        const bool refusesGarbage = !ui.LoadFont(UI::UIFontRole::Title, notAFont);

        // 그래도 제목은 그려진다: 배정되지 않은 역할은 먼저 등록된 다른 폰트로 간다.
        const Rendering::RenderFrame fallbackFrame = RunUIFrame(source, input, ui, [&] {
            ui.DrawLabel(
                { 8.0f, 0.0f, 200.0f, 24.0f }, "Title", Math::Color::White, 14.0f,
                UI::TextAlign::Left, UI::UIFontRole::Title);
        });
        const bool fallsBackToDefaultFont =
            fallbackFrame.GetDraws<Rendering::TextDraw>(Rendering::RenderPass::Transparent).size() == 1;

        // 실제 폰트 파일이 있으면 배정되고, 그 역할의 글자는 다른 폰트의 같은 글자와 다른
        // 아틀라스 슬롯에 산다 — 폰트 정체성이 글리프 키의 일부라는 뜻이다.
        std::vector<std::byte> fontBytes;
        {
            std::ifstream file(R"(C:\Windows\Fonts\consola.ttf)", std::ios::binary);
            if (file)
            {
                file.seekg(0, std::ios::end);
                const std::streamoff size = file.tellg();
                file.seekg(0, std::ios::beg);
                if (size > 0)
                {
                    fontBytes.resize(static_cast<std::size_t>(size));
                    file.read(reinterpret_cast<char*>(fontBytes.data()), size);
                }
            }
        }
        bool loadsRealFont = true;
        bool distinctFromDefault = true;
        if (!fontBytes.empty())
        {
            // 여기도 TestFont가 먼저 있어야 한다: 그래야 아래에서 빈 이름이 "test.console"이
            // 아니라 TestFont로 떨어지고, 둘의 아틀라스 슬롯이 정말 갈린다.
            auto distinctRasterizer = TestSupport::CreateTestTextRasterizer();
            if (!distinctRasterizer)
            {
                std::cout << "  font role tests: no bundled font, skipping that half\n";
                return Expect(refusesGarbage, "bytes that are not a font should be refused") &&
                    Expect(
                        fallsBackToDefaultFont,
                        "an unassigned role should still draw with the default font");
            }
            Rendering::TextRasterizationCache cache{ std::move(distinctRasterizer) };
            cache.BeginFrame();
            loadsRealFont = cache.RegisterFont("test.console", fontBytes);

            Platform::TextRasterizationRequest request;
            request.text = "MMM";
            request.fontSize = 18.0f;
            const std::shared_ptr<const Rendering::ShapedText> defaultFont = cache.Resolve(request);
            request.fontFamily = "test.console";
            const std::shared_ptr<const Rendering::ShapedText> registered = cache.Resolve(request);
            distinctFromDefault = loadsRealFont && defaultFont && registered &&
                !defaultFont->runs.empty() && !registered->runs.empty() &&
                defaultFont->runs.front().glyphs->front().u !=
                    registered->runs.front().glyphs->front().u;
        }
        else
        {
            std::cout << "  font role tests: no system font file to register, skipping that half\n";
        }

        return Expect(refusesGarbage, "bytes that are not a font should be refused") &&
            Expect(fallsBackToDefaultFont, "an unassigned role should still draw with the default font") &&
            Expect(loadsRealFont, "a real font file should register from its bytes") &&
            Expect(
                distinctFromDefault,
                "a registered font's glyphs should take their own atlas slots");
    }

    /// <summary>
    /// 시트에서 조각 하나만 꺼내 그리는 길이다. 타일 팔레트가 타일 그림을 보이는 방법이고,
    /// 확인할 것은 두 가지다: draw가 그 조각의 UV를 실어 나르는가, 그리고 사각형이 조각 크기와
    /// 무관하게 요청한 크기 그대로 나오는가. 후자는 공용 quad 배치가 프레임의 픽셀 크기로
    /// 사각형을 만들기 때문에 배율을 조각으로 나눠야 성립한다 — 나누지 않으면 16x16 시트에서
    /// 꺼낸 타일이 요청한 칸의 16분의 1로 그려진다.
    /// </summary>
    bool RunImageRegionTests()
    {
        using namespace GameEngine;

        FakeInput source;
        Runtime::Input input;
        UI::UIContext ui{ nullptr, nullptr };

        // 512x512 시트를 16x16으로 나눈 것 중 오른쪽 아래 칸이다.
        auto sheet = std::make_shared<Assets::TextureData>();
        sheet->id = Assets::MakeResourceId(Assets::ResourceIdDomain::Texture, 7);
        sheet->width = 512;
        sheet->height = 512;
        sheet->pixels.assign(static_cast<std::size_t>(512) * 512 * 4, std::byte{ 0xFF });

        constexpr float Cell = 1.0f / 16.0f;
        const Rendering::SpriteUVRect lastTile{ 15.0f * Cell, 15.0f * Cell, Cell, Cell };
        const UI::UIRect target{ 40.0f, 60.0f, 34.0f, 34.0f };

        const Rendering::RenderFrame frame = RunUIFrame(source, input, ui, [&] {
            ui.DrawImageRegion(target, sheet, lastTile);
        });
        const std::vector<const Rendering::SpriteDraw*> draws =
            frame.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Transparent);

        const bool oneDraw = draws.size() == 1;
        const bool carriesUv = oneDraw && draws[0]->uvRect.u == lastTile.u &&
            draws[0]->uvRect.v == lastTile.v && draws[0]->uvRect.width == Cell &&
            draws[0]->uvRect.height == Cell;

        // 배율은 조각 기준이다: 512/16 = 32픽셀 조각을 34픽셀 칸에 채우므로 34/32이다.
        // 텍스처 기준으로 나눴다면 34/512가 되어 타일이 보이지 않을 만큼 작아진다.
        const bool fillsTheCell = oneDraw &&
            std::abs(draws[0]->localToWorld.GetElement(0, 0) - 34.0f / 32.0f) < 1e-5f &&
            std::abs(draws[0]->localToWorld.GetElement(1, 1) - 34.0f / 32.0f) < 1e-5f;

        // 조각을 말하지 않은 이미지는 그대로 이미지 전체이고 배율도 텍스처 기준이다.
        const Rendering::RenderFrame wholeFrame = RunUIFrame(source, input, ui, [&] {
            ui.DrawImage(target, sheet);
        });
        const std::vector<const Rendering::SpriteDraw*> wholeDraws =
            wholeFrame.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Transparent);
        const bool wholeImageUnchanged = wholeDraws.size() == 1 &&
            wholeDraws[0]->uvRect.IsWholeImage() &&
            std::abs(wholeDraws[0]->localToWorld.GetElement(0, 0) - 34.0f / 512.0f) < 1e-5f;

        return Expect(oneDraw, "drawing an image region should produce one sprite draw") &&
            Expect(carriesUv, "an image region should carry the requested sub-rectangle") &&
            Expect(fillsTheCell, "an image region should fill its rectangle whatever the sheet size") &&
            Expect(
                wholeImageUnchanged,
                "an image without a region should still cover the whole texture");
    }

    /// <summary>
    /// UI 프레임은 자기 카메라를 잠그고 — 픽셀 직교, 장면 카메라 없음 — 선언된 위젯들을
    /// 스프라이트와 텍스트 draw로 싣는다.
    /// </summary>
    /// <summary>
    /// 선언 순서가 프레임의 그리기 목록에 그대로 남는지 고정한다.
    ///
    /// 떠 있는 창이 아래 패널을 덮는 근거가 이것 하나다: 셸은 도킹 패널을 먼저 선언하고 창의
    /// 몸통을 나중에 선언할 뿐, 그 사이에 z를 적지 않는다. 목록이 어디선가 정렬되면 그 약속이
    /// 조용히 깨지고, 증상은 "창 아래 패널이 창 위에 보인다"가 된다.
    ///
    /// 확인하지 않는 것: quad와 글자 사이의 순서. 프레임은 그 둘을 <b>다른 목록</b>으로 들고
    /// 있어 선언 순서로 비교되지 않으며, 한 패스 안에서 글자가 quad 위에 오는 것은 렌더러의
    /// 계약이다 — 에디터의 모든 패널과 레이블이 이미 그 계약 위에 서 있다.
    /// </summary>
    bool RunDeclarationOrderTests()
    {
        using namespace GameEngine;

        FakeInput source;
        Runtime::Input input;
        UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };

        // 아래 패널, 그 위에 뜬 창의 바탕 — 셸이 한 프레임에 선언하는 순서 그대로다. 크기는
        // 변환 행렬에 녹아 있으므로 둘을 가르는 표는 틴트로 둔다.
        constexpr float DockedTint = 0.2f;
        constexpr float WindowTint = 0.3f;
        const Rendering::RenderFrame frame = RunUIFrame(source, input, ui, [&] {
            ui.DrawPanel({ 0.0f, 300.0f, 800.0f, 200.0f }, { DockedTint, DockedTint, DockedTint, 1.0f });
            ui.DrawPanel({ 240.0f, 120.0f, 420.0f, 260.0f }, { WindowTint, WindowTint, WindowTint, 1.0f });
        });

        const std::vector<const Rendering::SpriteDraw*> sprites =
            frame.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Transparent);
        const bool bothDrawn = sprites.size() == 2 && sprites[0] && sprites[1];
        // 나중에 선언한 창의 바탕이 목록에서도 뒤에 온다 — 그래서 위에 그려진다.
        const bool windowIsLast = bothDrawn &&
            std::abs(sprites[0]->tint.r - DockedTint) < 0.01f &&
            std::abs(sprites[1]->tint.r - WindowTint) < 0.01f;

        return Expect(bothDrawn, "both panels should reach the frame") &&
            Expect(
                windowIsLast,
                "the later declared panel should come later in the frame's draw list");
    }

    bool RunFrameContentTests()
    {
        using namespace GameEngine;

        FakeInput source;
        Runtime::Input input;
        auto rasterizer = TestSupport::CreateTestTextRasterizer();
        if (!rasterizer)
        {
            std::cout << "  UI frame content tests skipped: no bundled font on this machine\n";
            return true;
        }
        UI::UIContext ui{ nullptr, std::move(rasterizer) };

        const Rendering::RenderFrame frame = RunUIFrame(source, input, ui, [&] {
            ui.DrawPanel({ 0.0f, 0.0f, 800.0f, 24.0f }, { 0.2f, 0.2f, 0.2f, 1.0f });
            ui.DrawLabel({ 8.0f, 0.0f, 200.0f, 24.0f }, "Title", Math::Color::White);
        });

        const bool hasLockedCamera = frame.GetCamera().has_value();
        const bool valid = frame.Validate().IsValid();
        const std::size_t spriteCount =
            frame.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Transparent).size();
        const std::size_t textCount =
            frame.GetDraws<Rendering::TextDraw>(Rendering::RenderPass::Transparent).size();

        return Expect(hasLockedCamera, "a UI frame should carry its own locked camera") &&
            Expect(valid, "a UI frame should validate") &&
            Expect(spriteCount == 1, "the panel should be one sprite draw") &&
            Expect(textCount == 1, "the label should be one text draw");
    }
}

    /// <summary>
    /// 긴 문구를 좁은 슬롯에 그려도 글리프의 오른쪽 끝이 사각형 안에 머무는지 확인한다.
    /// 그려진 글리프 좌표를 비교하여 이웃 요소 위로 글자가 넘치는지 창 없이 검사한다.
    /// </summary>
    bool RunTextFittingTests()
    {
        using namespace GameEngine;

        // 그려진 글자 블록의 오른쪽 끝이다. 글리프 사각형은 draw의 지역 좌표라 이동을 더한다.
        const auto rightmostEdge = [](const Rendering::RenderFrame& frame)
        {
            float right = -1e9f;
            for (const Rendering::TextDraw* const draw :
                 frame.GetDraws<Rendering::TextDraw>(Rendering::RenderPass::Transparent))
            {
                if (!draw->glyphs)
                {
                    continue;
                }
                const float originX = draw->localToWorld.GetElement(3, 0);
                for (const Rendering::TextGlyphQuad& glyph : *draw->glyphs)
                {
                    right = (std::max)(right, originX + glyph.centerX + glyph.width * 0.5f);
                }
            }
            return right;
        };
        const auto glyphCount = [](const Rendering::RenderFrame& frame)
        {
            std::size_t count = 0;
            for (const Rendering::TextDraw* const draw :
                 frame.GetDraws<Rendering::TextDraw>(Rendering::RenderPass::Transparent))
            {
                count += draw->glyphs ? draw->glyphs->size() : 0;
            }
            return count;
        };

        constexpr std::string_view SaveQuestion = "Unsaved changes - open anyway?";
        constexpr std::string_view PaletteHint = "Select an object with a Tilemap Renderer to paint.";

        FakeInput source;
        Runtime::Input input;
        UI::UIContext ui{ nullptr, TestSupport::CreateTestTextRasterizer() };

        // 좁은 슬롯이다. 두 문구 모두 이 폭보다 길다.
        const UI::UIRect narrow{ 20.0f, 40.0f, 140.0f, 20.0f };
        const Rendering::RenderFrame narrowQuestion = RunUIFrame(source, input, ui, [&] {
            ui.DrawLabel(narrow, SaveQuestion, Math::Color::White, 12.0f);
        });
        const Rendering::RenderFrame narrowHint = RunUIFrame(source, input, ui, [&] {
            ui.DrawLabel(narrow, PaletteHint, Math::Color::White, 12.0f);
        });

        // 넓은 슬롯이다. 안내서가 "넓은 자리로 옮기면 낫다"고 권하는 그 자리다.
        const UI::UIRect wide{ 20.0f, 40.0f, 600.0f, 20.0f };
        const Rendering::RenderFrame wideQuestion = RunUIFrame(source, input, ui, [&] {
            ui.DrawLabel(wide, SaveQuestion, Math::Color::White, 12.0f);
        });

        const float narrowRight = narrow.GetRight();
        const bool questionStaysInside = rightmostEdge(narrowQuestion) <= narrowRight;
        const bool hintStaysInside = rightmostEdge(narrowHint) <= narrowRight;

        // 넓은 자리에서는 줄어들 이유가 없다: 같은 문장이 좁은 자리보다 더 많은 글리프로
        // 그려지고, 그 블록은 여전히 자기 사각형 안이다.
        const bool wideShowsMore = glyphCount(wideQuestion) > glyphCount(narrowQuestion);
        const bool wideStaysInside = rightmostEdge(wideQuestion) <= wide.GetRight();

        // 잘림과 넘침은 다른 것이다. 좁은 자리에서 문장이 줄어들었다는 사실 자체도 함께 건다 —
        // 이 시험이 "아무것도 그리지 않아서" 통과하는 일이 없도록.
        const bool narrowActuallyTruncates =
            glyphCount(narrowQuestion) > 0 && glyphCount(narrowQuestion) < glyphCount(wideQuestion);

        return Expect(
                   questionStaysInside,
                   "the save question should not spill past its rect in a narrow slot") &&
            Expect(hintStaysInside, "the palette hint should not spill past its rect") &&
            Expect(wideShowsMore, "a wide slot should show more of the same sentence") &&
            Expect(wideStaysInside, "a wide slot should still keep its text inside") &&
            Expect(narrowActuallyTruncates, "a narrow slot should draw a shortened sentence");
    }

bool RunUIContextTests()
{
    return Expect(RunButtonTests(), "UI button tests should pass") &&
        Expect(RunTextFieldTests(), "UI text field tests should pass") &&
        Expect(RunTextEditCaretTests(), "UI text caret tests should pass") &&
        Expect(RunTextSelectionTests(), "UI text selection tests should pass") &&
        Expect(RunTextClipboardTests(), "UI text clipboard tests should pass") &&
        Expect(RunTextClickCaretTests(), "UI text click caret tests should pass") &&
        Expect(RunHoverOrderTests(), "UI hover order tests should pass") &&
        Expect(RunDeclarationOrderTests(), "UI declaration order tests should pass") &&
        Expect(RunRawPointerTests(), "UI raw pointer tests should pass") &&
        Expect(RunSelectableTests(), "UI selectable tests should pass") &&
        Expect(RunScrollTests(), "UI scroll tests should pass") &&
        Expect(RunInteractiveImageTests(), "UI interactive image tests should pass") &&
        Expect(RunImageRegionTests(), "UI image region tests should pass") &&
        Expect(RunButtonOwnershipTests(), "UI button ownership tests should pass") &&
        Expect(RunFrameContentTests(), "UI frame content tests should pass") &&
        Expect(RunFontRoleTests(), "UI font role tests should pass") &&
        Expect(RunExternalPointerTests(), "UI external pointer tests should pass") &&
        Expect(RunTextFittingTests(), "UI text fitting tests should pass");
}

static const TestSupport::Registration gUIContextTests{
    "UIContext", "UI context tests should pass", RunUIContextTests };
