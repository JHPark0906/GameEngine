#include "ModalKeyboardIsolationTests.h"

#include <memory>
#include <utility>

#include "Platform/IInput.h"
#include "Runtime/Button.h"
#include "Runtime/Canvas.h"
#include "Runtime/Dropdown.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/InputField.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RectTransform.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"
#include "Runtime/UIEventSystem.h"
#include "Runtime/UILayoutSystem.h"
#include "Runtime/UIWindow.h"
#include "TestSupport.h"

bool RunModalKeyboardIsolationTests()
{
    namespace Runtime = GameEngine::Runtime;
    namespace Platform = GameEngine::Platform;
    return TestSupport::ForEachUiScale([](const float scale)
    {
        Runtime::ObjectRegistry registry;
        Runtime::Input input;
        Runtime::RuntimeContext context(registry, input);
        Runtime::SceneManager manager(context);
        auto scene = std::make_unique<Runtime::Scene>(context, "Modal");
        auto* canvas = scene->CreateGameObject("Canvas");
        canvas->AddComponent<Runtime::Canvas>()->SetScaleFactor(scale);
        const auto add = [&](const char* name, Runtime::GameObject* parent)
        {
            auto* object = scene->CreateGameObject(name);
            static_cast<void>(object->GetTransform().SetParent(&parent->GetTransform()));
            auto* rect = object->AddComponent<Runtime::RectTransform>();
            rect->SetOffsetMin({ 0.0f, 0.0f });
            rect->SetOffsetMax({ 100.0f, 50.0f });
            return object;
        };
        auto* background = add("Background", canvas);
        auto* field = background->AddComponent<Runtime::InputField>();
        field->SetText("original");
        auto* choice = add("Choice", canvas)->AddComponent<Runtime::Dropdown>();
        choice->SetOptions({ "first", "second" });
        choice->SetValue(0);
        auto* modal = add("Modal", canvas);
        auto* window = modal->AddComponent<Runtime::UIWindow>();
        window->SetModal(true);
        auto* modalField = add("ModalField", modal)->AddComponent<Runtime::InputField>();
        modal->SetActive(false);
        if (!TestSupport::Expect(manager.AddScene(std::move(scene)) != 0, "modal fixture must load")) return false;
        Runtime::UIEventSystem events;
        Runtime::UILayoutSystem layout;
        const auto step = [&](const Platform::InputState& state)
        {
            input.BeginFrameWithState(state);
            layout.Synchronize(manager, 800.0f * scale, 600.0f * scale);
            return events.Synchronize(manager, input);
        };
        Platform::InputState state;
        state.hasFocus = true;
        state.cursor = { static_cast<int>(500.0f * scale), static_cast<int>(300.0f * scale) };
        field->RequestFocus();
        static_cast<void>(step(state));
        bool passed = TestSupport::Expect(field->IsFocused(), "background field must start focused");
        choice->Open();
        modal->SetActive(true);
        state.typedText = "leak";
        state.SetKey(Platform::Key::Down, true);
        state.SetKey(Platform::Key::Enter, true);
        const bool consumed = step(state);
        passed = TestSupport::Expect(consumed && !field->IsFocused() && field->GetText() == "original" &&
            !field->WasEditedThisFrame() && !field->WasSubmittedThisFrame() &&
            choice->GetValue() == 0 && !choice->WasValueChangedThisFrame() && !choice->IsOpen(),
            "opening a modal must stop background text, submit and dropdown keyboard input") && passed;

        state = {};
        state.hasFocus = true;
        modalField->RequestFocus();
        static_cast<void>(step(state));
        state.typedText = "inside";
        static_cast<void>(step(state));
        passed = TestSupport::Expect(modalField->IsFocused() && modalField->GetText() == "inside",
            "modal fields must continue receiving keyboard input") && passed;
        modal->SetActive(false);
        field->RequestFocus();
        state.typedText.clear();
        static_cast<void>(step(state));
        state.typedText = "after";
        static_cast<void>(step(state));
        return TestSupport::Expect(field->IsFocused() && field->GetText() == "originalafter",
            "closing a modal must allow the background field to take focus again") && passed;
    });
}

static const TestSupport::Registration gModalKeyboardIsolationTests{
    "UIEvent", "modal windows must isolate existing keyboard focus", RunModalKeyboardIsolationTests };
