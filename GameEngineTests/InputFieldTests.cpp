#include "InputFieldTests.h"

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>

#include "Platform/IInput.h"
#include "Runtime/Canvas.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/InputField.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RectTransform.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "Runtime/UIEventSystem.h"
#include "Runtime/UILayoutSystem.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEngine::Platform::Key;
    using GameEngine::Platform::MouseButton;
    using GameEngine::Runtime::InputField;

    /// <summary>
    /// 창 대신 이번 프레임의 입력을 그대로 말해 주는 원본이다. 텍스트 필드는 마우스와 키보드가
    /// 함께 있어야 시험할 수 있는데, 창 없이 그 둘을 만드는 방법이 이것뿐이다.
    /// </summary>
    class ScriptedInput final : public GameEngine::Platform::IInput
    {
    public:
        void ReadState(GameEngine::Platform::InputState& state) override
        {
            state = mState;
            mState.TakeAccumulated();
        }

        void SetKey(const Key key, const bool down)
        {
            mState.SetKey(key, down);
        }
        void SetMouseDown(const bool down)
        {
            mState.SetMouseButton(MouseButton::Left, down);
        }
        void SetTypedText(std::string text) { mState.typedText = std::move(text); }
        void SetCompositionText(std::string text) { mState.compositionText = std::move(text); }
        void SetCursor(const int x, const int y)
        {
            mState.cursor.x = x;
            mState.cursor.y = y;
        }

        /// <summary>한 프레임이 지난 것처럼 만든다: 소비되는 것들은 다음 프레임에 남지 않는다.</summary>
        void EndFrame()
        {
            mState.typedText.clear();
        }

    private:
        GameEngine::Platform::InputState mState;
    };
}

bool RunInputFieldTests()
{
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::TextRenderer;
    using GameEngine::Runtime::UIEventSystem;
    using GameEngine::Runtime::UILayoutSystem;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "InputFields");
    GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    static_cast<void>(canvasObject->AddComponent<GameEngine::Runtime::Canvas>());

    // 나란히 놓인 필드 둘. 배치는 Canvas 아래로 흐르므로 그 자식이어야 자리를 받는다.
    const auto addField = [&scene, canvasObject](
        const char* const name, const float left, const float right)
    {
        GameObject* const object = scene->CreateGameObject(name);
        static_cast<void>(object->GetTransform().SetParent(&canvasObject->GetTransform()));
        RectTransform* const rect = object->AddComponent<RectTransform>();
        rect->SetAnchorMin({ 0.0f, 0.0f });
        rect->SetAnchorMax({ 0.0f, 0.0f });
        rect->SetOffsetMin({ left, 0.0f });
        rect->SetOffsetMax({ right, 40.0f });
        static_cast<void>(object->AddComponent<TextRenderer>());
        return object->AddComponent<InputField>();
    };
    InputField* const first = addField("First", 0.0f, 100.0f);
    InputField* const second = addField("Second", 120.0f, 220.0f);
    TextRenderer* const firstText =
        first ? first->GetGameObject()->GetComponent<TextRenderer>() : nullptr;

    const unsigned int sceneId = sceneManager.AddScene(std::move(scene));
    if (!Expect(sceneId != 0 && first && second && firstText, "the input field scene should assemble"))
    {
        return false;
    }

    const UILayoutSystem layout;
    UIEventSystem events;
    ScriptedInput scripted;
    const auto step = [&]()
    {
        input.BeginFrame(scripted);
        layout.Synchronize(sceneManager, 800.0f, 600.0f);
        static_cast<void>(events.Synchronize(sceneManager, input));
        scripted.EndFrame();
    };

    // 클릭이 포커스를 준다. 누르지 않은 프레임에는 아무도 포커스를 갖지 않는다.
    scripted.SetCursor(10, 10);
    step();
    const bool quietWithoutClick = !first->IsFocused() && !second->IsFocused();

    scripted.SetMouseDown(true);
    step();
    scripted.SetMouseDown(false);
    step();
    const bool clickFocuses = first->IsFocused() && !second->IsFocused();

    // 포커스를 쥔 필드만 타이핑을 받는다.
    scripted.SetTypedText("ab");
    step();
    const bool typingReachesFocused =
        first->GetText() == "ab" && second->GetText().empty() && first->WasEditedThisFrame();

    // 그리고 다음 프레임에는 "이번 프레임에 편집됐다"가 다시 거짓이다.
    step();
    const bool editFlagIsPerFrame = !first->WasEditedThisFrame();

    // 다른 필드를 누르면 포커스가 옮겨 간다.
    scripted.SetCursor(150, 10);
    scripted.SetMouseDown(true);
    step();
    scripted.SetMouseDown(false);
    scripted.SetTypedText("c");
    step();
    const bool focusMoves = second->IsFocused() && !first->IsFocused() &&
        second->GetText() == "c" && first->GetText() == "ab";

    // 빈 자리를 누르면 아무도 포커스를 갖지 않는다 — 그 뒤의 타이핑은 어디에도 들어가지 않는다.
    scripted.SetCursor(600, 400);
    scripted.SetMouseDown(true);
    step();
    scripted.SetMouseDown(false);
    scripted.SetTypedText("z");
    step();
    const bool clickingAwayDropsFocus = !first->IsFocused() && !second->IsFocused() &&
        first->GetText() == "ab" && second->GetText() == "c";

    // 보이는 글자는 TextRenderer가 그린다: 필드는 자기 값을 거기 싣는다.
    const bool textReachesRenderer = firstText->GetText() == "ab";

    // 조합 중인 글자는 값이 아니라 화면에만 있다. 포커스를 되찾아 조합을 걸어 본다.
    scripted.SetCursor(10, 10);
    scripted.SetMouseDown(true);
    step();
    scripted.SetMouseDown(false);
    scripted.SetCompositionText("한");
    step();
    const bool compositionShowsButIsNotValue =
        first->GetText() == "ab" && firstText->GetText() == "ab한";

    // 확정되면 조합은 사라지고 같은 내용이 타이핑으로 도착한다.
    scripted.SetCompositionText({});
    scripted.SetTypedText("한");
    step();
    const bool commitBecomesValue = first->GetText() == "ab한" && firstText->GetText() == "ab한";

    // Enter는 확정이다: 표시를 남기고 포커스를 놓는다.
    scripted.SetKey(Key::Enter, true);
    step();
    scripted.SetKey(Key::Enter, false);
    const bool enterSubmits = first->WasSubmittedThisFrame() && !first->IsFocused();

    // 입력을 받지 않게 된 필드는 포커스를 쥐고 있을 수 없다.
    scripted.SetCursor(10, 10);
    scripted.SetMouseDown(true);
    step();
    scripted.SetMouseDown(false);
    const bool refocused = first->IsFocused();
    first->SetInteractable(false);
    step();
    scripted.SetTypedText("x");
    step();
    const bool disabledDropsFocus = refocused && !first->IsFocused() && first->GetText() == "ab한";

    // 밖에서 값을 갈아 끼우면 캐럿은 새 내용의 끝이다 — 옛 자리는 새 문자열의 글자 한가운데일
    // 수 있다.
    first->SetText("가나");
    const bool setTextMovesCaret = first->GetCaret() == std::string("가나").size();

    return Expect(quietWithoutClick, "a field should not take focus without a click") &&
        Expect(clickFocuses, "clicking a field should give it keyboard focus") &&
        Expect(typingReachesFocused, "typing should reach only the focused field") &&
        Expect(editFlagIsPerFrame, "the edited flag should last one frame") &&
        Expect(focusMoves, "clicking another field should move the focus") &&
        Expect(clickingAwayDropsFocus, "clicking away from every field should drop the focus") &&
        Expect(textReachesRenderer, "the field's text should reach its TextRenderer") &&
        Expect(
            compositionShowsButIsNotValue,
            "composing text should show on screen without entering the value") &&
        Expect(commitBecomesValue, "a committed composition should arrive as typed text") &&
        Expect(enterSubmits, "Enter should submit and release the focus") &&
        Expect(disabledDropsFocus, "a field that stops taking input should lose the focus") &&
        Expect(setTextMovesCaret, "setting the text from outside should move the caret to its end");
}

static const TestSupport::Registration gInputFieldTests{
    "UIEvent", "input field tests should pass", RunInputFieldTests };
