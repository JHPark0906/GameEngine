#include "ConfirmationTests.h"

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <ranges>

#include "Diagnostics/Debug.h"
#include "Rules/EditorConfirmation.h"
#include "TestSupport.h"
#include "Platform/ITextMeasure.h"
#include "Platform/ITextRasterizer.h"
#include "Views/EditorConfirmationView.h"
#include "Runtime/Button.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RectTransform.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/UILayoutSystem.h"
#include "Runtime/UIWindow.h"

namespace
{
    using TestSupport::Expect;

    /// <summary>답이 몇 번 왔고 무엇이었는지 세는 물음이다.</summary>
    struct Answered
    {
        int count = 0;
        std::optional<std::size_t> chosen;
    };

    /// <summary>글자 폭을 글자 수로 셈하는 잣대다. 배치가 줄을 접는 데 그것이면 족하다.</summary>
    class CountingTextMeasure final : public GameEngine::Platform::ITextMeasure
    {
    public:
        void BeginFrame() override {}

        [[nodiscard]] GameEngine::Platform::TextExtent Measure(
            const GameEngine::Platform::TextRasterizationRequest& request) override
        {
            constexpr float WidthPerCharacter = 7.0f;
            const float width = static_cast<float>(request.text.size()) * WidthPerCharacter;
            if (request.maxWidth <= 0.0f || width <= request.maxWidth)
            {
                return { width, request.fontSize };
            }
            // 접힌다. 줄 수만큼 높아지는 것이 배치가 이 잣대에서 얻는 전부다.
            const float lines = std::ceil(width / request.maxWidth);
            return { request.maxWidth, request.fontSize * lines * request.lineSpacing };
        }
    };

    /// <summary>돌아가는 동안 기록된 일반 로그를 모은다.</summary>
    class LogCollector final
    {
    public:
        LogCollector()
            : mId(GameEngine::Diagnostics::Debug::AddLogListener(
                  [this](const GameEngine::Diagnostics::LogEntry& entry)
                  {
                      if (entry.level == GameEngine::Diagnostics::LogLevel::Message)
                      {
                          mLines.push_back(entry.message);
                      }
                  }))
        {
        }

        ~LogCollector() { GameEngine::Diagnostics::Debug::RemoveLogListener(mId); }

        LogCollector(const LogCollector&) = delete;
        LogCollector& operator=(const LogCollector&) = delete;

        [[nodiscard]] std::string FirstContaining(const std::string& text) const
        {
            for (const std::string& line : mLines)
            {
                if (line.find(text) != std::string::npos)
                {
                    return line;
                }
            }
            return {};
        }

    private:
        std::vector<std::string> mLines;
        GameEngine::Diagnostics::Debug::LogListenerId mId;
    };

    [[nodiscard]] GameEditor::ConfirmationRequest MakeRequest(
        const char* const question, Answered& record)
    {
        GameEditor::ConfirmationRequest request;
        request.question = question;
        request.choices = { "Yes" };
        request.cancelLabel = "Cancel";
        request.onAnswered = [&record](const std::optional<std::size_t> chosen)
        {
            ++record.count;
            record.chosen = chosen;
        };
        return request;
    }
}

bool RunConfirmationQueueTests()
{
    std::cout << "running confirmation queue tests\n";

    // 서 있는 것이 없으면 아무것도 서 있지 않다. 그리는 쪽이 이것으로 물음의 유무를 안다.
    GameEditor::ConfirmationQueue queue;
    bool passed = Expect(!queue.IsAsking(), "an empty queue should not be asking");
    // 답할 것이 없을 때 답해도 아무 일이 없어야 한다. 그리는 쪽의 클릭이 한 프레임 늦게
    // 도착하는 일이 있고, 그때 없는 물음에 답이 붙으면 안 된다.
    queue.Answer(0);
    passed = Expect(!queue.IsAsking(), "answering an empty queue should do nothing") && passed;

    Answered first;
    Answered second;
    queue.Ask(MakeRequest("first", first));
    queue.Ask(MakeRequest("second", second));

    // 한 번에 하나만 선다. 둘이 함께 서면 사람은 어느 것에 답하는지 모른다.
    const GameEditor::ConfirmationRequest* const current = queue.GetCurrent();
    passed = Expect(current != nullptr && current->question == "first",
        "the first question asked should be the one standing") && passed;
    passed = Expect(queue.GetPendingCount() == 2, "the second should be waiting its turn") &&
        passed;
    passed = Expect(second.count == 0, "the waiting question should not have been answered") &&
        passed;

    // 답은 정확히 한 번, 그리고 그 뒤 다음 것이 선다.
    queue.Answer(0);
    std::cout << "  after answering the first: first answered " << first.count
              << " time(s), standing question is "
              << (queue.GetCurrent() ? queue.GetCurrent()->question : std::string("none")) << "\n";
    passed = Expect(first.count == 1, "the answer should arrive exactly once") && passed;
    passed = Expect(
        first.chosen.has_value() && *first.chosen == 0, "the chosen button should come through") &&
        passed;
    passed = Expect(
        queue.GetCurrent() != nullptr && queue.GetCurrent()->question == "second",
        "the next question should stand once the first is answered") && passed;

    // 답한 물음에 다시 답해도 그 콜백은 다시 불리지 않는다. 되돌릴 수 없는 동작이 두 번
    // 일어나는 것이 이 계약이 막는 것이다.
    queue.Answer(0);
    passed = Expect(second.count == 1, "the second answer should also arrive exactly once") &&
        passed;
    queue.Answer(0);
    passed = Expect(
        first.count == 1 && second.count == 1,
        "answering again after the queue is empty should not repeat any answer") && passed;

    // 취소는 비어 있는 답이다. 부르는 쪽이 if (chosen) 하나로 다룰 수 있어야 한다.
    Answered cancelled;
    queue.Ask(MakeRequest("cancelled", cancelled));
    queue.CancelCurrent();
    std::cout << "  cancelling gave an answer: " << (cancelled.count == 1 ? "once" : "not once")
              << ", with a value: " << (cancelled.chosen.has_value() ? "yes" : "no") << "\n";
    passed = Expect(cancelled.count == 1, "cancelling should still answer exactly once") && passed;
    passed = Expect(
        !cancelled.chosen.has_value(), "cancelling should mean doing nothing") && passed;

    // 답을 받은 자리에서 다시 묻는 것이 흔하다 — "지울까요" 뒤에 "정말로?"가 온다. 그 새
    // 물음이 방금 끝난 것 뒤에 제대로 서야 한다.
    Answered chained;
    Answered followUp;
    GameEditor::ConfirmationRequest chaining = MakeRequest("chained", chained);
    chaining.onAnswered = [&queue, &chained, &followUp](const std::optional<std::size_t> chosen)
    {
        ++chained.count;
        chained.chosen = chosen;
        queue.Ask(MakeRequest("follow up", followUp));
    };
    queue.Ask(std::move(chaining));
    queue.Answer(0);
    passed = Expect(chained.count == 1, "the chaining question should answer once") && passed;
    passed = Expect(
        queue.GetCurrent() != nullptr && queue.GetCurrent()->question == "follow up",
        "a question asked from an answer should stand next") && passed;
    queue.Answer(0);
    passed = Expect(followUp.count == 1, "the follow up should answer once") && passed;
    passed = Expect(!queue.IsAsking(), "the queue should be empty again") && passed;

    return passed;
}

bool RunConfirmationViewTests()
{
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::UILayoutSystem;
    using GameEngine::Runtime::UIWindow;

    std::cout << "running confirmation view tests\n";

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "Confirmation");
    Scene* const scenePointer = scene.get();

    GameEditor::ConfirmationQueue queue;
    GameEditor::EditorConfirmationView view(queue);
    view.Build(*scenePointer);

    if (sceneManager.AddScene(std::move(scene)) == 0)
    {
        return Expect(false, "the confirmation scene should become the active scene");
    }

    CountingTextMeasure textMeasure;
    const UILayoutSystem layout;
    constexpr float SurfaceWidth = 1280.0f;
    constexpr float SurfaceHeight = 720.0f;

    // 물을 것이 없으면 창은 서지 않는다.
    view.Synchronize(1.0f, SurfaceWidth, SurfaceHeight, &textMeasure);
    layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight, &textMeasure);
    bool passed = Expect(!view.IsShowing(), "with nothing to ask, the window should stay away");

    Answered answered;
    GameEditor::ConfirmationRequest request = MakeRequest("Delete this scene?", answered);
    request.title = "Delete Scene";
    request.choices = { "Delete", "Keep a copy" };
    request.cancelLabel = "Cancel";
    queue.Ask(std::move(request));

    view.Synchronize(1.0f, SurfaceWidth, SurfaceHeight, &textMeasure);
    layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight, &textMeasure);
    passed = Expect(view.IsShowing(), "a question should raise the window") && passed;
    // 진행 둘과 취소 하나가 함께 선다.
    passed = Expect(
        view.GetVisibleButtonCount() == 3, "both choices and the cancel should be shown") && passed;

    // 창과 버튼의 사각형을 읽는다. 사람이 고를 수 없게 되는 실패 — 겹침, 창 밖으로 나감 — 을
    // 눈이 아니라 산술로 잡는다.
    std::vector<const RectTransform*> buttonRects;
    const RectTransform* windowRect = nullptr;
    for (const GameObject* const object : scenePointer->GetGameObjects() | std::views::values |
             std::views::transform([](const auto& owned) { return owned.get(); }))
    {
        if (!object || !object->IsActiveInHierarchy())
        {
            continue;
        }
        if (object->GetName() == "Confirmation")
        {
            windowRect = object->GetComponent<RectTransform>();
        }
        if (object->GetName().starts_with("ConfirmationChoice") &&
            object->GetName().find("Label") == std::string::npos)
        {
            if (const RectTransform* const rect = object->GetComponent<RectTransform>())
            {
                buttonRects.push_back(rect);
            }
        }
    }

    passed = Expect(windowRect != nullptr, "the window should have a rectangle") && passed;
    std::cout << "  placed " << buttonRects.size() << " button rectangles\n";
    passed = Expect(buttonRects.size() == 3, "three buttons should have been placed") && passed;
    if (!windowRect || buttonRects.size() != 3)
    {
        return false;
    }

    std::ranges::sort(
        buttonRects, {},
        [](const RectTransform* const rect) { return rect->GetResolvedRect().x; });
    std::cout << "  window " << windowRect->GetResolvedRect().width << " wide, buttons";
    for (const RectTransform* const rect : buttonRects)
    {
        std::cout << " [" << rect->GetResolvedRect().x << "," << rect->GetResolvedRect().GetRight()
                  << "]";
    }
    std::cout << "\n";

    for (std::size_t index = 0; index < buttonRects.size(); ++index)
    {
        const RectTransform::Rect rect = buttonRects[index]->GetResolvedRect();
        passed = Expect(rect.width > 0.0f, "a button should have width") && passed;
        passed = Expect(
            rect.x >= windowRect->GetResolvedRect().x &&
                rect.GetRight() <= windowRect->GetResolvedRect().GetRight() + 0.5f,
            "a button should stay inside the window") && passed;
        if (index > 0)
        {
            passed = Expect(
                rect.x >= buttonRects[index - 1]->GetResolvedRect().GetRight() - 0.5f,
                "buttons should not overlap") && passed;
        }
    }

    // 모달로 선다. 답하기 전에 뒤의 것을 만지지 못하는 것이 팝업으로 묻는 이유의 절반이다.
    const GameObject* const windowObject = windowRect->GetGameObject();
    const UIWindow* const window =
        windowObject ? windowObject->GetComponent<UIWindow>() : nullptr;
    passed = Expect(
        window != nullptr && window->IsModal(), "the confirmation should stand as a modal") &&
        passed;

    // 물음이 길어지면 창이 그만큼 높아진다. 높이가 고정이면 긴 물음은 잘리고, 잘린 물음은
    // 사람이 무엇에 답하는지 모르게 만든다.
    const float shortHeight = windowRect->GetResolvedRect().height;
    Answered longAnswered;
    GameEditor::ConfirmationRequest longRequest = MakeRequest(
        "This scene is referenced by three other scenes, and deleting it will leave those"
        " references pointing at nothing.\nThey will be reported as missing the next time the"
        " project is opened.\nDelete it anyway?",
        longAnswered);
    longRequest.title = "Delete Scene";
    longRequest.choices = { "Delete" };
    longRequest.cancelLabel = "Cancel";
    queue.Ask(std::move(longRequest));
    // 서 있는 물음이 아직 답을 받지 않았으므로, 긴 물음은 그 뒤에 선다. 앞의 것을 답해 준다.
    queue.Answer(std::nullopt);
    view.Synchronize(1.0f, SurfaceWidth, SurfaceHeight, &textMeasure);
    layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight, &textMeasure);
    const float tallHeight = windowRect->GetResolvedRect().height;
    std::cout << "  window height with a short question: " << shortHeight
              << ", with a long one: " << tallHeight << "\n";
    passed = Expect(
        tallHeight > shortHeight, "a longer question should make the window taller") && passed;

    // 답이 오면 창이 사라진다.
    queue.Answer(0);
    view.Synchronize(1.0f, SurfaceWidth, SurfaceHeight, &textMeasure);
    layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight, &textMeasure);
    std::cout << "  after the answer, the window is "
              << (view.IsShowing() ? "still up" : "gone") << "\n";
    passed = Expect(!view.IsShowing(), "answering should take the window away") && passed;
    passed = Expect(answered.count == 1, "the answer should have arrived once") && passed;

    return passed;
}

bool RunConfirmationWidthTests()
{
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::TextRenderer;
    using GameEngine::Runtime::UILayoutSystem;

    std::cout << "running confirmation width tests\n";

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "ConfirmationWidth");
    Scene* const scenePointer = scene.get();

    GameEditor::ConfirmationQueue queue;
    GameEditor::EditorConfirmationView view(queue);
    view.Build(*scenePointer);
    if (sceneManager.AddScene(std::move(scene)) == 0)
    {
        return Expect(false, "the width scene should become the active scene");
    }

    CountingTextMeasure textMeasure;
    const UILayoutSystem layout;
    constexpr float SurfaceWidth = 1280.0f;
    constexpr float SurfaceHeight = 720.0f;

    const auto windowWidth = [scenePointer]() -> float
    {
        for (const auto& owned : scenePointer->GetGameObjects() | std::views::values)
        {
            const GameObject* const object = owned.get();
            if (object && object->GetName() == "Confirmation")
            {
                if (const RectTransform* const rect = object->GetComponent<RectTransform>())
                {
                    return rect->GetResolvedRect().width;
                }
            }
        }
        return 0.0f;
    };
    const auto firstLabel = [scenePointer]() -> std::string
    {
        for (const auto& owned : scenePointer->GetGameObjects() | std::views::values)
        {
            const GameObject* const object = owned.get();
            if (object && object->GetName() == "ConfirmationChoiceLabel0")
            {
                if (const TextRenderer* const text = object->GetComponent<TextRenderer>())
                {
                    return text->GetText();
                }
            }
        }
        return {};
    };

    Answered shortAnswered;
    GameEditor::ConfirmationRequest shortRequest = MakeRequest("Delete?", shortAnswered);
    shortRequest.choices = { "Yes" };
    shortRequest.cancelLabel = "No";
    queue.Ask(std::move(shortRequest));
    view.Synchronize(1.0f, SurfaceWidth, SurfaceHeight, &textMeasure);
    layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight, &textMeasure);
    const float narrow = windowWidth();
    const std::string narrowLabel = firstLabel();
    queue.Answer(std::nullopt);

    Answered longAnswered;
    GameEditor::ConfirmationRequest longRequest = MakeRequest("Delete?", longAnswered);
    longRequest.choices = { "Delete it and every scene that points at it" };
    longRequest.cancelLabel = "No";
    queue.Ask(std::move(longRequest));
    view.Synchronize(1.0f, SurfaceWidth, SurfaceHeight, &textMeasure);
    layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight, &textMeasure);
    const float wide = windowWidth();
    const std::string wideLabel = firstLabel();
    queue.Answer(std::nullopt);

    std::cout << "  short label: window " << narrow << " wide, label \"" << narrowLabel
              << "\"\n  long label: window " << wide << " wide, label \"" << wideLabel << "\"\n";

    // 긴 라벨은 창을 넓힌다. 넓히지 않으면 라벨이 잘리고, 잘린 선택지는 무엇인지 알 수 없다.
    bool passed = Expect(wide > narrow, "a longer label should widen the window");
    // 그리고 그 라벨은 아직 잘리지 않았다 — 상한에 닿지 않았으므로.
    passed = Expect(
        wideLabel == "Delete it and every scene that points at it",
        "a label that fits under the cap should not be shortened") && passed;

    // 상한에 걸리는 라벨이다. 창은 거기서 멈추고 라벨은 말줄임된다.
    Answered hugeAnswered;
    GameEditor::ConfirmationRequest hugeRequest = MakeRequest("Delete?", hugeAnswered);
    hugeRequest.choices = { std::string(400, 'x') };
    hugeRequest.cancelLabel = "No";
    queue.Ask(std::move(hugeRequest));
    view.Synchronize(1.0f, SurfaceWidth, SurfaceHeight, &textMeasure);
    layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight, &textMeasure);
    const float capped = windowWidth();
    const std::string cappedLabel = firstLabel();
    queue.Answer(std::nullopt);

    std::cout << "  huge label: window " << capped << " wide (cap "
              << SurfaceWidth * 0.8f << "), label ends with \""
              << cappedLabel.substr(cappedLabel.size() >= 4 ? cappedLabel.size() - 4 : 0)
              << "\", length " << cappedLabel.size() << "\n";
    passed = Expect(
        capped <= SurfaceWidth * 0.8f + 0.5f, "the window should stop at the cap") && passed;
    // 멈춘 자리에서 라벨은 잘리고 말줄임표가 붙는다. 잘린 채 아무 표시가 없으면 사람은 그것이
    // 전부인 줄 안다.
    passed = Expect(
        cappedLabel.size() < 400, "a label that cannot fit should be shortened") && passed;
    passed = Expect(
        cappedLabel.ends_with("..."), "a shortened label should say that it was shortened") &&
        passed;

    return passed;
}

bool RunConfirmationDisabledChoiceTests()
{
    using GameEngine::Runtime::Button;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::UILayoutSystem;

    std::cout << "running confirmation disabled choice tests\n";

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "ConfirmationDisabled");
    Scene* const scenePointer = scene.get();

    GameEditor::ConfirmationQueue queue;
    GameEditor::EditorConfirmationView view(queue);
    view.Build(*scenePointer);
    if (sceneManager.AddScene(std::move(scene)) == 0)
    {
        return Expect(false, "the disabled choice scene should become the active scene");
    }

    CountingTextMeasure textMeasure;
    const UILayoutSystem layout;
    constexpr float SurfaceWidth = 1280.0f;
    constexpr float SurfaceHeight = 720.0f;

    Answered answered;
    GameEditor::ConfirmationRequest request = MakeRequest(
        "Three scenes point at assets by path, and one of them cannot be read.", answered);
    request.choices = { "Migrate", "Show me the scenes" };
    request.cancelLabel = "Not now";
    // 막힌 진행이다. 무엇이 막고 있는지는 물음의 글이 말하고, 버튼은 흐리게 남는다.
    request.disabledChoices = { 0 };
    queue.Ask(std::move(request));

    view.Synchronize(1.0f, SurfaceWidth, SurfaceHeight, &textMeasure);
    layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight, &textMeasure);

    const auto buttonAt = [scenePointer](const std::size_t index) -> const Button*
    {
        const std::string name = "ConfirmationChoice" + std::to_string(index);
        for (const auto& owned : scenePointer->GetGameObjects() | std::views::values)
        {
            const GameObject* const object = owned.get();
            if (object && object->GetName() == name)
            {
                return object->GetComponent<Button>();
            }
        }
        return nullptr;
    };

    const Button* const blocked = buttonAt(0);
    const Button* const open = buttonAt(1);
    std::cout << "  blocked choice is interactable: "
              << (blocked && blocked->IsInteractable() ? "yes" : "no")
              << ", the other one: " << (open && open->IsInteractable() ? "yes" : "no")
              << ", both shown: "
              << (blocked && open && blocked->GetGameObject() &&
                         blocked->GetGameObject()->IsActiveInHierarchy() &&
                         open->GetGameObject() && open->GetGameObject()->IsActiveInHierarchy()
                     ? "yes"
                     : "no")
              << "\n";

    bool passed = Expect(blocked != nullptr && open != nullptr, "both choices should be built");
    if (!blocked || !open)
    {
        return false;
    }
    // 보인다. 감추면 사람이 그 길이 있다는 것을 모른다.
    passed = Expect(
        blocked->GetGameObject() && blocked->GetGameObject()->IsActiveInHierarchy(),
        "a blocked choice should still be shown") && passed;
    // 그러나 눌리지 않는다.
    passed = Expect(!blocked->IsInteractable(), "a blocked choice should not be pressable") &&
        passed;
    passed = Expect(open->IsInteractable(), "the choice that is open should stay pressable") &&
        passed;
    // 그리고 답이 되지 않는다 — 눌러도 아무 일이 없는 버튼이 되지 않게, 아예 후보에서 뺀다.
    passed = Expect(answered.count == 0, "nothing should have been answered yet") && passed;

    return passed;
}

bool RunConfirmationScaleTests()
{
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::UILayoutSystem;

    std::cout << "running confirmation scale tests\n";

    // 같은 물음을 배율마다 세우고, 풀린 폭이 배율에 정확히 비례하는지 본다.
    float widthAtOne = 0.0f;
    const bool measured = TestSupport::ForEachUiScale(
        [&widthAtOne](const float scale)
        {
            GameEngine::Runtime::ObjectRegistry registry;
            GameEngine::Runtime::Input input;
            GameEngine::Runtime::RuntimeContext context{ registry, input };
            GameEngine::Runtime::SceneManager sceneManager{ context };

            auto scene = std::make_unique<Scene>(context, "ConfirmationScale");
            Scene* const scenePointer = scene.get();

            GameEditor::ConfirmationQueue queue;
            GameEditor::EditorConfirmationView view(queue);
            view.Build(*scenePointer);
            if (sceneManager.AddScene(std::move(scene)) == 0)
            {
                return Expect(false, "the scale scene should become the active scene");
            }

            CountingTextMeasure textMeasure;
            const UILayoutSystem layout;
            // 면은 물리 픽셀이다. 배율이 오르면 같은 창에 더 많은 픽셀이 들어간다.
            const float surfaceWidth = 1280.0f * scale;
            const float surfaceHeight = 720.0f * scale;

            Answered answered;
            GameEditor::ConfirmationRequest request = MakeRequest("Delete?", answered);
            request.choices = { "Delete" };
            request.cancelLabel = "Cancel";
            queue.Ask(std::move(request));

            view.Synchronize(scale, surfaceWidth, surfaceHeight, &textMeasure);
            layout.Synchronize(sceneManager, surfaceWidth, surfaceHeight, &textMeasure);

            float width = 0.0f;
            for (const auto& owned : scenePointer->GetGameObjects() | std::views::values)
            {
                const GameObject* const object = owned.get();
                if (object && object->GetName() == "Confirmation")
                {
                    if (const RectTransform* const rect = object->GetComponent<RectTransform>())
                    {
                        width = rect->GetResolvedRect().width;
                    }
                }
            }
            std::cout << "  at scale " << scale << ": window " << width << " wide\n";

            bool passed = Expect(width > 0.0f, "the window should have a width at every scale");
            if (scale == 1.0f)
            {
                widthAtOne = width;
                return passed;
            }
            // 배율 두 배면 폭도 두 배다. 네 배면 어딘가에서 배율을 두 번 곱한 것이다.
            const float expected = widthAtOne * scale;
            passed = Expect(
                std::abs(width - expected) < 1.0f,
                "the window should scale exactly once, not twice") && passed;
            return passed;
        });

    return measured;
}

bool RunConfirmationLogTests()
{
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::UILayoutSystem;

    std::cout << "running confirmation log tests\n";

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "ConfirmationLog");
    Scene* const scenePointer = scene.get();

    GameEditor::ConfirmationQueue queue;
    GameEditor::EditorConfirmationView view(queue);
    view.Build(*scenePointer);
    if (sceneManager.AddScene(std::move(scene)) == 0)
    {
        return Expect(false, "the log scene should become the active scene");
    }

    CountingTextMeasure textMeasure;
    const UILayoutSystem layout;
    constexpr float SurfaceWidth = 1280.0f;
    constexpr float SurfaceHeight = 720.0f;

    Answered answered;
    GameEditor::ConfirmationRequest request = MakeRequest("Restore it?", answered);
    request.title = "Unsaved work was recovered";
    request.choices = { "Restore it", "Delete the copy" };
    request.cancelLabel = "Decide later";
    queue.Ask(std::move(request));

    std::string logged;
    {
        // 릴리스에서는 일반 메시지가 기본으로 꺼져 있고 편집기가 부팅하며 켠다. 시험은 그
        // 부팅을 거치지 않으므로 여기서 켠다 — 켜지 않으면 릴리스에서만 이 시험이 붉어지고,
        // 그것은 코드가 아니라 시험의 문제다.
        const bool messagesWereEnabled = GameEngine::Diagnostics::Debug::AreMessagesEnabled();
        GameEngine::Diagnostics::Debug::SetMessagesEnabled(true);
        const LogCollector collector;
        // 자리는 배치가 푼 뒤에야 안다. 그래서 두 프레임이 필요하다 — 첫 프레임이 세우고
        // 두 번째가 그 자리를 적는다.
        view.Synchronize(1.0f, SurfaceWidth, SurfaceHeight, &textMeasure);
        layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight, &textMeasure);
        view.Synchronize(1.0f, SurfaceWidth, SurfaceHeight, &textMeasure);
        layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight, &textMeasure);
        logged = collector.FirstContaining("Confirmation shown");
        GameEngine::Diagnostics::Debug::SetMessagesEnabled(messagesWereEnabled);
    }
    std::cout << "  logged: " << logged << "\n";

    bool passed = Expect(!logged.empty(), "a question that stands should say so in the log");
    // 제목과 라벨 셋이 전부 있어야 밖에서 라벨로 버튼을 고를 수 있다. 라벨 검사가 지우는
    // 버튼을 거부하려면 그 라벨이 로그에 있어야 한다.
    for (const char* const expected :
        { "Unsaved work was recovered", "Restore it", "Delete the copy", "Decide later" })
    {
        passed = Expect(
            logged.find(expected) != std::string::npos,
            "the log should name every label the person can press") && passed;
    }
    // 사각형이 라벨마다 붙어 있어야 그 자리를 누를 수 있다. 셋이므로 셋이다.
    const std::size_t places = std::ranges::count(logged, '@');
    std::cout << "  places in the line: " << places << "\n";
    passed = Expect(places == 3, "every label should carry the place it was drawn") && passed;
    // 그 자리는 실제로 그려진 자리다. 0,0,0,0이면 아직 풀리지 않은 것을 적은 것이다.
    passed = Expect(
        logged.find("@(0,0,0,0)") == std::string::npos,
        "the places should be the resolved ones, not empty rectangles") && passed;

    return passed;
}

static const TestSupport::Registration gConfirmationQueueTests{
    "EditorDocument", "confirmation queue tests should pass", RunConfirmationQueueTests };

static const TestSupport::Registration gConfirmationViewTests{
    "EditorDocument", "confirmation view tests should pass", RunConfirmationViewTests };

static const TestSupport::Registration gConfirmationWidthTests{
    "EditorDocument", "confirmation width tests should pass", RunConfirmationWidthTests };

static const TestSupport::Registration gConfirmationDisabledChoiceTests{
    "EditorDocument", "confirmation disabled choice tests should pass", RunConfirmationDisabledChoiceTests };

static const TestSupport::Registration gConfirmationScaleTests{
    "EditorDocument", "confirmation scale tests should pass", RunConfirmationScaleTests };

static const TestSupport::Registration gConfirmationLogTests{
    "EditorDocument", "confirmation log tests should pass", RunConfirmationLogTests };
