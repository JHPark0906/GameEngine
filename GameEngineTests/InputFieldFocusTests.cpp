#include "InputFieldFocusTests.h"

#include <cstddef>
#include <memory>
#include <string>

#include "Platform/IInput.h"
#include "Runtime/Canvas.h"
#include "Runtime/Button.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/InputField.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RectTransform.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/UIEventSystem.h"
#include "Runtime/UILayoutSystem.h"
#include "Runtime/UIWindow.h"
#include "Runtime/Transform.h"
#include "Runtime/TextRenderer.h"
#include "TestSupport.h"

bool RunInputFieldFocusTests()
{
    using namespace GameEngine;
    Runtime::ObjectRegistry registry;
    Runtime::Input input;
    Runtime::RuntimeContext context(registry, input);
    Runtime::SceneManager manager(context);
    auto scene = std::make_unique<Runtime::Scene>(context, "Requested focus");
    auto* canvas = scene->CreateGameObject("Canvas");
    static_cast<void>(canvas->AddComponent<Runtime::Canvas>());
    auto* firstObject = scene->CreateGameObject("First");
    static_cast<void>(firstObject->GetTransform().SetParent(&canvas->GetTransform()));
    static_cast<void>(firstObject->AddComponent<Runtime::RectTransform>());
    auto* first = firstObject->AddComponent<Runtime::InputField>();
    auto* secondObject = scene->CreateGameObject("Second");
    static_cast<void>(secondObject->GetTransform().SetParent(&canvas->GetTransform()));
    static_cast<void>(secondObject->AddComponent<Runtime::RectTransform>());
    auto* second = secondObject->AddComponent<Runtime::InputField>();
    auto* modal = scene->CreateGameObject("Modal");
    static_cast<void>(modal->GetTransform().SetParent(&canvas->GetTransform()));
    static_cast<void>(modal->AddComponent<Runtime::RectTransform>());
    modal->AddComponent<Runtime::UIWindow>()->SetModal(true);
    modal->SetActive(false);
    static_cast<void>(manager.AddScene(std::move(scene)));
    Runtime::UIEventSystem events;
    Runtime::UILayoutSystem layout;
    const auto step = [&](const Platform::InputState& state)
    {
        input.BeginFrameWithState(state);
        layout.Synchronize(manager, 800, 600);
        static_cast<void>(events.Synchronize(manager, input));
    };
    Platform::InputState enter;
    enter.SetKey(Platform::Key::Enter, true);
    first->RequestFocus();
    step(enter);
    bool passed = TestSupport::Expect(first->IsFocused() && !first->WasSubmittedThisFrame(),
        "Enter that opens a requested field must focus it without immediately submitting");
    Platform::InputState typing;
    typing.typedText = "한글";
    step(typing);
    passed &= TestSupport::Expect(first->GetText() == "한글", "requested focus must receive subsequent typed text");
    Platform::InputState imeCommit;
    imeCommit.SetKey(Platform::Key::Enter, true);
    imeCommit.typedText = "확정";
    imeCommit.imeHandledEnter = true;
    step(imeCommit);
    passed &= TestSupport::Expect(first->IsFocused() && !first->WasSubmittedThisFrame() && first->GetText() == "한글확정",
        "IME Enter with a same-frame committed result must insert text without submitting");
    Platform::InputState imeBackspace;
    imeBackspace.SetKey(Platform::Key::Backspace, true);
    imeBackspace.imeHandledKeys[static_cast<std::size_t>(Platform::Key::Backspace)] = true;
    step(imeBackspace);
    passed &= TestSupport::Expect(first->GetText() == "한글확정",
        "Backspace already handled by IME must not delete committed input again");
    second->RequestFocus();
    step({});
    passed &= TestSupport::Expect(!first->IsFocused() && second->IsFocused(),
        "programmatic focus must remain exclusive and transfer away from the previous field");
    step(enter);
    passed &= TestSupport::Expect(second->WasSubmittedThisFrame() && !second->IsFocused(),
        "a later Enter must still submit a requested field normally");
    first->SetInteractable(false);
    first->RequestFocus();
    step({});
    passed &= TestSupport::Expect(!first->IsFocused(), "disabled fields cannot request focus");
    first->SetInteractable(true);
    modal->SetActive(true);
    first->RequestFocus();
    step({});
    passed &= TestSupport::Expect(!first->IsFocused(), "a focus request cannot bypass an active modal window");
    modal->SetActive(false);
    step({});
    passed &= TestSupport::Expect(!first->IsFocused(), "a rejected modal focus request must not replay after closing it");
    return passed;
}

static const TestSupport::Registration gInputFieldFocusTests{
    "UIEvent", "requested input focus should preserve Enter and modal rules", RunInputFieldFocusTests };

namespace
{
    bool RunQueuedTextSurvivesFocusTransfer()
    {
        using namespace GameEngine;
        return TestSupport::ForEachUiScale([](const float scale)
        {
            bool passed = true;
            for (const bool ime : { false, true })
                for (int destination = 0; destination < 4; ++destination)
                {
                    Runtime::ObjectRegistry registry;
                    Runtime::Input input;
                    Runtime::RuntimeContext context(registry, input);
                    Runtime::SceneManager manager(context);
                    auto scene = std::make_unique<Runtime::Scene>(context, "Focus transfer text");
                    auto* canvas = scene->CreateGameObject("Canvas");
                    canvas->AddComponent<Runtime::Canvas>()->SetScaleFactor(scale);
                    const auto rectangle = [&](const char* name, const Math::Vector2& first, const Math::Vector2& last)
                    {
                        auto* object = scene->CreateGameObject(name);
                        static_cast<void>(object->GetTransform().SetParent(&canvas->GetTransform()));
                        auto* rect = object->AddComponent<Runtime::RectTransform>();
                        rect->SetOffsetMin(first);
                        rect->SetOffsetMax(last);
                        return object;
                    };
                    // The new recipient is visited first, so duplicate suppression cannot depend
                    // on having already delivered text while visiting the old field.
                    auto* nextObject = rectangle("Next", { 10, 80 }, { 190, 130 });
                    static_cast<void>(nextObject->AddComponent<Runtime::TextRenderer>());
                    auto* next = nextObject->AddComponent<Runtime::InputField>();
                    next->SetText("새");
                    auto* previousObject = rectangle("Previous", { 10, 10 }, { 190, 60 });
                    auto* display = previousObject->AddComponent<Runtime::TextRenderer>();
                    auto* previous = previousObject->AddComponent<Runtime::InputField>();
                    previous->SetText("앞");
                    auto* button = rectangle("Button", { 230, 10 }, { 300, 60 });
                    static_cast<void>(button->AddComponent<Runtime::Button>());
                    static_cast<void>(manager.AddScene(std::move(scene)));
                    Runtime::UIEventSystem events;
                    Runtime::UILayoutSystem layout;
                    const auto step = [&](const Platform::InputState& state)
                    {
                        input.BeginFrameWithState(state);
                        layout.Synchronize(manager, 400 * scale, 200 * scale);
                        static_cast<void>(events.Synchronize(manager, input));
                    };
                    previous->RequestFocus();
                    step({});
                    const bool replaceSelection = destination % 2 == 0;
                    if (replaceSelection)
                    {
                        Platform::InputState selectAll;
                        selectAll.SetKey(Platform::Key::Control, true);
                        selectAll.SetKey(Platform::Key::A, true);
                        step(selectAll);
                        step({});
                    }
                    Platform::InputState preedit;
                    if (ime) preedit.compositionText = "글";
                    step(preedit);
                    Platform::InputState transfer;
                    transfer.typedText = ime ? "글" : "x";
                    if (ime)
                    {
                        transfer.SetKey(Platform::Key::Enter, true);
                        transfer.MarkKeyHandledByIme(Platform::Key::Enter);
                    }
                    if (destination == 0) next->RequestFocus();
                    else
                    {
                        const float x = destination == 1 ? 30.0f : destination == 2 ? 250.0f : 350.0f;
                        const float y = destination == 1 ? 100.0f : destination == 2 ? 30.0f : 170.0f;
                        transfer.cursor = { static_cast<int>(x * scale), static_cast<int>(y * scale) };
                        transfer.SetMouseButton(Platform::MouseButton::Left, true);
                    }
                    step(transfer);
                    const std::string expected = (replaceSelection ? std::string{} : std::string("앞")) +
                        (ime ? "글" : "x");
                    passed &= TestSupport::Expect(previous->GetText() == expected && display->GetText() == expected &&
                        previous->WasEditedThisFrame() && !previous->IsFocused() && !previous->WasSubmittedThisFrame(),
                        "queued IME and ordinary text must reach the previous field before request/click/blank focus changes");
                    passed &= TestSupport::Expect(next->GetText() == "새" && next->IsFocused() == (destination < 2) &&
                        !next->WasEditedThisFrame() && !next->WasSubmittedThisFrame(),
                        "the new field must receive focus without duplicating the previous field's queued text or IME Enter");
                    // The following read belongs to the newly focused field, not its predecessor.
                    Platform::InputState after;
                    if (destination < 2) after.typedText = "다음";
                    step(after);
                    passed &= TestSupport::Expect(previous->GetText() == expected && !previous->WasEditedThisFrame() &&
                        next->GetText() == (destination < 2 ? "새다음" : "새"),
                        "subsequent typing must follow the new focus while the previous commit remains exactly once");
                }
            return passed;
        });
    }
}

static const TestSupport::Registration gInputFieldFocusTransferText{
    "UIEvent", "queued typing must survive input focus transfer", RunQueuedTextSurvivesFocusTransfer };
