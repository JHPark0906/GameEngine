#include "InputFieldBackspaceRepeatTests.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>

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
#include "Runtime/Transform.h"
#include "Runtime/UIEventSystem.h"
#include "Runtime/UILayoutSystem.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;

    class Fixture final
    {
    public:
        explicit Fixture(const float scale)
            : mContext(mRegistry, mInput), mManager(mContext), mScale(scale)
        {
            auto scene = std::make_unique<Runtime::Scene>(mContext, "Backspace repeat");
            auto* canvas = scene->CreateGameObject("Canvas");
            canvas->AddComponent<Runtime::Canvas>()->SetScaleFactor(scale);
            const auto addField = [&](const char* name)
            {
                auto* object = scene->CreateGameObject(name);
                static_cast<void>(object->GetTransform().SetParent(&canvas->GetTransform()));
                static_cast<void>(object->AddComponent<Runtime::RectTransform>());
                return object->AddComponent<Runtime::InputField>();
            };
            first = addField("First");
            second = addField("Second");
            static_cast<void>(mManager.AddScene(std::move(scene)));
            state.hasFocus = true;
            first->RequestFocus();
            Step(0.0f);
        }

        void Step(const float seconds)
        {
            mInput.BeginFrameWithState(state);
            mLayout.Synchronize(mManager, 800.0f * mScale, 600.0f * mScale);
            static_cast<void>(mEvents.Synchronize(mManager, mInput, nullptr, seconds));
            state.TakeAccumulated();
        }

        Platform::InputState state;
        Runtime::InputField* first = nullptr;
        Runtime::InputField* second = nullptr;

    private:
        Runtime::ObjectRegistry mRegistry;
        Runtime::Input mInput;
        Runtime::RuntimeContext mContext;
        Runtime::SceneManager mManager;
        Runtime::UIEventSystem mEvents;
        Runtime::UILayoutSystem mLayout;
        float mScale;
    };

    bool CheckTiming(const float scale)
    {
        Fixture fixture(scale);
        fixture.first->SetText(std::string(32, 'a'));
        fixture.state.SetKey(Platform::Key::Backspace, true);
        fixture.Step(0.0f);
        bool passed = TestSupport::Expect(fixture.first->GetText().size() == 31,
            "Backspace must immediately erase once on its physical press");
        fixture.Step(0.39f);
        passed &= TestSupport::Expect(fixture.first->GetText().size() == 31,
            "held Backspace must wait for its initial repeat delay");
        fixture.Step(0.01f);
        passed &= TestSupport::Expect(fixture.first->GetText().size() == 30,
            "held Backspace must erase again after 400 ms without another key edge");
        fixture.Step(0.1f);
        passed &= TestSupport::Expect(fixture.first->GetText().size() == 28,
            "a 100 ms frame must apply two elapsed repeats, not one frame-based deletion");
        fixture.state.SetKey(Platform::Key::Backspace, false);
        fixture.Step(1.0f);
        passed &= TestSupport::Expect(fixture.first->GetText().size() == 28,
            "key release must stop repeats immediately");
        fixture.state.SetKey(Platform::Key::Backspace, true);
        fixture.state.SetKey(Platform::Key::Backspace, false);
        fixture.Step(0.0f);
        fixture.Step(1.0f);
        passed &= TestSupport::Expect(fixture.first->GetText().size() == 27,
            "a complete press/release between reads must erase once and must not arm repeats");

        Fixture slow(scale);
        Fixture fast(scale);
        for (Fixture* const item : { &slow, &fast })
        {
            item->first->SetText(std::string(64, 'b'));
            item->state.SetKey(Platform::Key::Backspace, true);
            item->Step(0.0f);
        }
        for (int index = 0; index < 10; ++index) slow.Step(0.1f);
        for (int index = 0; index < 100; ++index) fast.Step(0.01f);
        passed &= TestSupport::Expect(slow.first->GetText() == fast.first->GetText() &&
            slow.first->GetText().size() == 50,
            "equal one-second holds at 10 and 100 fps must erase the same 14 characters");

        Fixture hitch(scale);
        hitch.first->SetText(std::string(128, 'c'));
        hitch.state.SetKey(Platform::Key::Backspace, true);
        hitch.Step(0.0f);
        hitch.Step(std::numeric_limits<float>::quiet_NaN());
        hitch.Step(std::numeric_limits<float>::infinity());
        hitch.Step(-1.0f);
        passed &= TestSupport::Expect(hitch.first->GetText().size() == 127,
            "invalid elapsed times must not create repeat input");
        hitch.Step(10.0f);
        hitch.Step(0.001f);
        passed &= TestSupport::Expect(hitch.first->GetText().size() == 119,
            "long stalls must erase at most eight catch-up characters and discard repeat debt");
        hitch.Step(0.049f);
        passed &= TestSupport::Expect(hitch.first->GetText().size() == 118,
            "after bounded catch-up, repeats must resume at the normal interval");
        return passed;
    }

    bool CheckFocus(const float scale)
    {
        Fixture fixture(scale);
        fixture.first->SetText("first");
        fixture.second->SetText("second");
        fixture.state.SetKey(Platform::Key::Backspace, true);
        fixture.Step(0.0f);
        fixture.second->RequestFocus();
        fixture.Step(1.0f);
        fixture.Step(1.0f);
        bool passed = TestSupport::Expect(fixture.first->GetText() == "firs" && fixture.second->GetText() == "second",
            "focus transfer must not give a newly focused field an already held Backspace");
        fixture.state.SetKey(Platform::Key::Backspace, false);
        fixture.Step(0.0f);
        fixture.state.SetKey(Platform::Key::Backspace, true);
        fixture.Step(0.0f);
        passed &= TestSupport::Expect(fixture.second->GetText() == "secon",
            "a fresh press in the new field must erase normally");
        fixture.second->SetInteractable(false);
        fixture.Step(1.0f);
        fixture.second->SetInteractable(true);
        fixture.second->RequestFocus();
        fixture.Step(1.0f);
        passed &= TestSupport::Expect(fixture.second->GetText() == "secon",
            "disabling and refocusing a field must cancel its previous held repeat");
        fixture.state.SetKey(Platform::Key::Backspace, false);
        fixture.Step(0.0f);
        fixture.state.SetKey(Platform::Key::Backspace, true);
        fixture.Step(0.0f);
        fixture.state.hasFocus = false;
        fixture.Step(1.0f);
        fixture.state.hasFocus = true;
        fixture.Step(1.0f);
        passed &= TestSupport::Expect(fixture.second->GetText() == "seco",
            "window focus loss must disarm repeats even if a platform snapshot retains the held key");
        fixture.state.SetKey(Platform::Key::Backspace, false);
        fixture.Step(0.0f);
        fixture.state.SetKey(Platform::Key::Backspace, true);
        fixture.Step(0.0f);
        fixture.state.SetKey(Platform::Key::Enter, true);
        fixture.Step(0.0f);
        fixture.state.SetKey(Platform::Key::Enter, false);
        fixture.second->RequestFocus();
        fixture.Step(1.0f);
        passed &= TestSupport::Expect(fixture.second->GetText() == "sec",
            "submitting then reopening the same field must not resurrect a held repeat");
        return passed;
    }

    bool CheckUtf8AndIme(const float scale)
    {
        Fixture fixture(scale);
        fixture.first->SetText("A한😀Z");
        fixture.state.SetKey(Platform::Key::Backspace, true);
        fixture.Step(0.0f);
        bool passed = TestSupport::Expect(fixture.first->GetText() == "A한😀", "initial deletion must erase a whole UTF-8 character");
        fixture.Step(0.4f);
        passed &= TestSupport::Expect(fixture.first->GetText() == "A한" && fixture.first->GetCaret() == 4,
            "repeated deletion must erase a four-byte character without splitting the preceding Hangul");
        fixture.Step(0.05f);
        passed &= TestSupport::Expect(fixture.first->GetText() == "A" && fixture.first->GetCaret() == 1,
            "repeated deletion must erase a complete three-byte Hangul syllable");

        fixture.state.SetKey(Platform::Key::Backspace, false);
        fixture.Step(0.0f);
        fixture.first->SetText("가나다");
        fixture.state.compositionText = "한";
        fixture.state.SetKey(Platform::Key::Backspace, true);
        fixture.Step(0.0f);
        fixture.Step(1.0f);
        fixture.state.compositionText.clear();
        fixture.state.MarkKeyHandledByIme(Platform::Key::Backspace);
        fixture.Step(1.0f);
        fixture.Step(0.39f);
        passed &= TestSupport::Expect(fixture.first->GetText() == "가나다",
            "IME deletion and its final empty-composition frame must not delete committed text or accumulate repeats");
        fixture.Step(0.01f);
        passed &= TestSupport::Expect(fixture.first->GetText() == "가나",
            "holding Backspace after composition ends must resume only after a fresh initial delay");

        fixture.state.SetKey(Platform::Key::Backspace, false);
        fixture.Step(0.0f);
        fixture.state.SetKey(Platform::Key::Control, true);
        fixture.state.SetKey(Platform::Key::A, true);
        fixture.state.SetKey(Platform::Key::Backspace, true);
        fixture.Step(0.0f);
        fixture.state.SetKey(Platform::Key::Control, false);
        fixture.state.SetKey(Platform::Key::A, false);
        fixture.Step(0.4f);
        passed &= TestSupport::Expect(fixture.first->GetText().empty() && fixture.first->GetCaret() == 0,
            "Backspace must delete a selection once and safely repeat on an empty field");
        fixture.first->SetText("abcdefgh");
        fixture.state.typedText = "UV";
        fixture.Step(0.1f);
        passed &= TestSupport::Expect(fixture.first->GetText() == "abcdefgh",
            "catch-up deletions must not replay the frame's typed text for each repeat");
        return passed;
    }
}

bool RunInputFieldBackspaceRepeatTests()
{
    return TestSupport::ForEachUiScale([](const float scale)
    {
        const bool timing = CheckTiming(scale);
        const bool focus = CheckFocus(scale);
        const bool utf8AndIme = CheckUtf8AndIme(scale);
        return timing && focus && utf8AndIme;
    });
}

static const TestSupport::Registration gInputFieldBackspaceRepeatTests{
    "UIEvent", "held Backspace should repeat by elapsed time without crossing focus or IME ownership", RunInputFieldBackspaceRepeatTests };
