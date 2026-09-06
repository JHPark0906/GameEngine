#include <utility>
#include <vector>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "../GameEngine/Runtime/Button.h"
#include "../GameEngine/Rendering/RenderFrame.h"
#include "../GameEngine/Rendering/RenderFrameBuilder.h"
#include "../GameEngine/Rendering/TextRasterizationCache.h"
#include "../GameEngine/SceneRendering/SceneRenderPass.h"
#include "../GameEngine/Platform/IAudioOutput.h"
#include "../GameEngine/Platform/PlatformServices.h"
#include "../GameEngine/Platform/ITextMeasure.h"
#include "../GameEngine/Runtime/Canvas.h"
#include "../GameEngine/Runtime/Dropdown.h"
#include "../GameEngine/Runtime/TextRenderer.h"
#include "../GameEngine/Runtime/Game.h"
#include "../GameEngine/Runtime/ContentFit.h"
#include "../GameEngine/Runtime/GameObject.h"
#include "../GameEngine/Math/Color.h"
#include "Rules/EditorPanelCommon.h"
#include "../GameEngine/Platform/IInput.h"
#include "../GameEngine/Runtime/Input.h"
#include "../GameEngine/Runtime/InputField.h"
#include "../GameEngine/Runtime/SpriteRenderer.h"
#include "../GameEngine/Runtime/ObjectRegistry.h"
#include "../GameEngine/Runtime/RectTransform.h"
#include "../GameEngine/Runtime/RuntimeContext.h"
#include "../GameEngine/Runtime/Scene.h"
#include "../GameEngine/Runtime/SceneManager.h"
#include "../GameEngine/Runtime/ScrollRect.h"
#include "../GameEngine/Runtime/Transform.h"
#include "../GameEngine/Runtime/UIEventSystem.h"
#include "../GameEngine/Runtime/UILayoutSystem.h"

#include "UIEventTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEngine::Runtime::Button;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::UIPointerRouter;

}

/// <summary>
/// 포인터 상태 기계다. 어려운 부분이 좌표가 아니라 시간이므로 — 누른 순간과 뗀 순간 사이에
/// 커서가 어디를 지났는지가 클릭의 주인을 정한다 — 그 사이를 프레임 단위로 고정한다.
///
/// 요소를 id로만 다루는 덕에 장면도 화면도 없이 선다.
/// </summary>
bool RunUIPointerRouterTests()
{
    constexpr unsigned int First = 11;
    constexpr unsigned int Second = 22;

    // 아무것도 누르지 않은 채 커서만 올리면 hover뿐이다.
    {
        UIPointerRouter router;
        const UIPointerRouter::Result result = router.Update({ First, false, false });
        if (!Expect(
                result.hoveredId == First && result.pressedId == 0 && result.clickedId == 0,
                "a cursor over an element should hover it and nothing else"))
        {
            return false;
        }
    }

    // 누르고 그 자리에서 떼면 클릭이다.
    {
        UIPointerRouter router;
        const UIPointerRouter::Result pressed = router.Update({ First, true, false });
        const UIPointerRouter::Result released = router.Update({ First, false, true });
        if (!Expect(
                pressed.pressedId == First && released.clickedId == First &&
                    router.GetCapturedId() == 0,
                "pressing and releasing on the same element should click it and let go"))
        {
            return false;
        }
    }

    // 한 프레임 안에 눌렸다 떼어진 빠른 클릭도 클릭이다. 누름보다 뗌을 먼저 보면 사라진다.
    {
        UIPointerRouter router;
        const UIPointerRouter::Result result = router.Update({ First, true, true });
        if (!Expect(
                result.clickedId == First, "a press and release in one frame should still click"))
        {
            return false;
        }
    }

    // 누른 채 밖으로 나가면 눌린 모습이 풀리지만 잡음은 남고, 돌아오면 다시 눌린 모습이 된다.
    {
        UIPointerRouter router;
        static_cast<void>(router.Update({ First, true, false }));
        const UIPointerRouter::Result outside = router.Update({ 0, false, false });
        const bool keptCapture = router.GetCapturedId() == First;
        const UIPointerRouter::Result back = router.Update({ First, false, false });
        if (!Expect(
                outside.pressedId == 0 && outside.hoveredId == 0 && keptCapture &&
                    back.pressedId == First,
                "dragging out of a pressed element should keep the capture and come back pressed"))
        {
            return false;
        }
    }

    // 밖에서 떼는 것은 취소다. 사람이 실수를 무를 수 있는 유일한 방법이라 클릭이 아니어야 한다.
    {
        UIPointerRouter router;
        static_cast<void>(router.Update({ First, true, false }));
        const UIPointerRouter::Result released = router.Update({ 0, false, true });
        if (!Expect(
                released.clickedId == 0 && router.GetCapturedId() == 0,
                "releasing outside the pressed element should cancel rather than click"))
        {
            return false;
        }
    }

    // 잡혀 있는 동안 다른 요소 위를 지나도 그 요소는 아무것도 받지 못한다. 받으면 누르지도
    // 않은 버튼이 눌린 것처럼 보이고, 거기서 떼면 누른 적 없는 버튼이 클릭된다.
    {
        UIPointerRouter router;
        static_cast<void>(router.Update({ First, true, false }));
        const UIPointerRouter::Result over = router.Update({ Second, false, false });
        const UIPointerRouter::Result released = router.Update({ Second, false, true });
        if (!Expect(
                over.hoveredId == 0 && over.pressedId == 0 && released.clickedId == 0,
                "a captured pointer should not give another element hover, press or click"))
        {
            return false;
        }
    }

    // 잡고 있던 요소가 사라지면 놓아야 한다. 놓지 않으면 그 id는 다시 오지 않으므로 포인터가
    // 영영 잡힌 채로 남는다.
    {
        UIPointerRouter router;
        static_cast<void>(router.Update({ First, true, false }));
        router.ReleaseCapture();
        const UIPointerRouter::Result result = router.Update({ Second, false, false });
        if (!Expect(
                result.hoveredId == Second,
                "releasing the capture should let another element be hovered again"))
        {
            return false;
        }
    }

    return true;
}

/// <summary>
/// 장면에서 겹친 버튼의 입력 우선순위와 비활성 버튼이 아래 요소를 가리지 않는지 확인한다.
/// 실제 픽셀로 표시되는 모습은 이 검사에 포함하지 않는다.
/// </summary>
bool RunUIEventSystemTests()
{
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::UIEventSystem;
    using GameEngine::Runtime::UILayoutSystem;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "UIEvents");

    // 배치는 Canvas를 면의 뿌리로 삼아 그 아래로 흐르므로, 버튼은 Canvas의 자식이어야 자리를
    // 받는다.
    GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    static_cast<void>(canvasObject->AddComponent<GameEngine::Runtime::Canvas>());

    // 같은 자리에 겹친 버튼 둘. 계층 순서의 뒤가 위이므로 두 번째가 커서를 받아야 한다.
    const auto addButton = [&scene, canvasObject](const char* const name)
    {
        GameObject* const object = scene->CreateGameObject(name);
        static_cast<void>(object->GetTransform().SetParent(&canvasObject->GetTransform()));
        RectTransform* const rect = object->AddComponent<RectTransform>();
        rect->SetAnchorMin({ 0.0f, 0.0f });
        rect->SetAnchorMax({ 0.0f, 0.0f });
        rect->SetOffsetMin({ 0.0f, 0.0f });
        rect->SetOffsetMax({ 100.0f, 50.0f });
        return object->AddComponent<Button>();
    };
    Button* const lower = addButton("Lower");
    Button* const upper = addButton("Upper");

    const unsigned int sceneId = sceneManager.AddScene(std::move(scene));
    if (!Expect(sceneId != 0 && lower && upper, "the UI event scene should assemble"))
    {
        return false;
    }

    const UILayoutSystem layout;
    UIEventSystem events;
    const auto step = [&layout, &events, &sceneManager, &input]()
    {
        layout.Synchronize(sceneManager, 800.0f, 600.0f);
        return events.Synchronize(sceneManager, input);
    };

    // 커서는 두 사각형이 겹친 자리에 있다. 입력은 창이 주는 것이므로, 여기서는 배치만 돌리고
    // 판정 결과를 본다 — 마우스 위치를 가짜로 넣을 자리가 Input에 없으므로 원점(0,0)이며 그
    // 자리는 두 사각형 안이다.
    const bool consumed = step();
    const bool upperWins = upper->IsHovered() && !lower->IsHovered();
    if (!Expect(
            consumed && upperWins,
            "the later sibling should win the cursor where two buttons overlap"))
    {
        return false;
    }

    // 위 버튼이 입력을 받지 않게 되면 아래 버튼이 대신 받는다. 눌리지 않는 버튼이 클릭을 삼키고
    // 아무 일도 하지 않는 것이 UI에서 가장 흔한 "고장처럼 보이는 정상"이다.
    upper->SetInteractable(false);
    static_cast<void>(step());
    if (!Expect(
            lower->IsHovered() && !upper->IsHovered(),
            "a non-interactable button should let the one beneath it take the cursor"))
    {
        return false;
    }

    // 버튼이 하나도 없으면 UI는 이 포인터를 가져가지 않는다 — 그 답이 있어야 같은 클릭을 다른
    // 쪽이 자기 것으로 쓸 수 있다.
    lower->SetInteractable(false);
    if (!Expect(!step(), "with nothing interactable the UI should not claim the pointer"))
    {
        return false;
    }

    return true;
}

/// <summary>
/// 입력 후보는 위젯 종류가 아니라 계층의 그리기 순서에 따라 선택되어야 한다.
/// 아래에 그려져 가려진 요소가 위 요소의 클릭을 가져가면 안 된다.
/// </summary>
bool RunHierarchyOrderBeatsKindTests()
{
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::InputField;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::UIEventSystem;
    using GameEngine::Runtime::UILayoutSystem;

    // 커서는 원점에 있다. 두 요소를 같은 자리에 포개고, 어느 쪽을 나중에 — 그래서 위에 — 두느냐만
    // 바꾼다. 장면마다 매니저를 따로 세우는 이유는 활성 장면이 순서 없는 맵이라, 한 매니저에 둘을
    // 담으면 훑는 순서가 정해지지 않기 때문이다.
    const auto buttonTakesCursorWhenDrawnLast = [](const bool buttonOnTop)
    {
        GameEngine::Runtime::ObjectRegistry registry;
        GameEngine::Runtime::Input input;
        GameEngine::Runtime::RuntimeContext context{ registry, input };
        GameEngine::Runtime::SceneManager sceneManager{ context };

        auto scene = std::make_unique<Scene>(context, "KindOrder");
        GameObject* const canvasObject = scene->CreateGameObject("Canvas");
        static_cast<void>(canvasObject->AddComponent<GameEngine::Runtime::Canvas>());

        const auto place = [&scene, canvasObject](const char* const name)
        {
            GameObject* const object = scene->CreateGameObject(name);
            static_cast<void>(object->GetTransform().SetParent(&canvasObject->GetTransform()));
            RectTransform* const rect = object->AddComponent<RectTransform>();
            rect->SetAnchorMin({ 0.0f, 0.0f });
            rect->SetAnchorMax({ 0.0f, 0.0f });
            rect->SetOffsetMin({ 0.0f, 0.0f });
            rect->SetOffsetMax({ 100.0f, 50.0f });
            return object;
        };

        Button* button = nullptr;
        if (buttonOnTop)
        {
            static_cast<void>(place("Field")->AddComponent<InputField>());
            button = place("Button")->AddComponent<Button>();
        }
        else
        {
            button = place("Button")->AddComponent<Button>();
            static_cast<void>(place("Field")->AddComponent<InputField>());
        }
        if (button == nullptr || sceneManager.AddScene(std::move(scene)) == 0)
        {
            return false;
        }

        const UILayoutSystem layout;
        UIEventSystem events;
        layout.Synchronize(sceneManager, 800.0f, 600.0f);
        static_cast<void>(events.Synchronize(sceneManager, input));
        return button->IsHovered();
    };

    return Expect(
            buttonTakesCursorWhenDrawnLast(true),
            "a button drawn over a field should take the cursor, not the field") &&
        Expect(
            !buttonTakesCursorWhenDrawnLast(false),
            "a field drawn over a button should take the cursor, not the button");
}

bool RunScrollRectTests()
{
    using GameEngine::Runtime::ContentFit;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::ScrollRect;
    using GameEngine::Runtime::UILayoutSystem;

    // 밀 수 있는 범위는 넘치는 만큼이고, 넘치지 않으면 0이다. 이 규칙이 없으면 짧은 목록이
    // 화면 밖으로 밀려 사라지고, 그 상태에서는 되돌릴 방법이 보이지 않는다.
    const bool clampsIntoRange =
        ScrollRect::ClampAxis(1000.0f, 100.0f, 300.0f) == 200.0f &&
        ScrollRect::ClampAxis(-50.0f, 100.0f, 300.0f) == 0.0f &&
        ScrollRect::ClampAxis(50.0f, 300.0f, 100.0f) == 0.0f;
    if (!Expect(clampsIntoRange, "scrolling should clamp into the overflow and never below zero"))
    {
        return false;
    }

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };
    auto scene = std::make_unique<Scene>(context, "Scrolling");

    GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    static_cast<void>(canvasObject->AddComponent<GameEngine::Runtime::Canvas>());

    // viewport: 화면 위쪽 200픽셀.
    GameObject* const viewportObject = scene->CreateGameObject("Viewport");
    static_cast<void>(viewportObject->GetTransform().SetParent(&canvasObject->GetTransform()));
    RectTransform* const viewportRect = viewportObject->AddComponent<RectTransform>();
    viewportRect->SetOffsetMin({ 0.0f, 0.0f });
    viewportRect->SetOffsetMax({ 300.0f, 200.0f });
    ScrollRect* const scrollRect = viewportObject->AddComponent<ScrollRect>();

    // content: 크기를 선언하지 않는다. 자식이 몇인지가 그 크기다.
    GameObject* const contentObject = scene->CreateGameObject("Content");
    static_cast<void>(contentObject->GetTransform().SetParent(&viewportObject->GetTransform()));
    RectTransform* const contentRect = contentObject->AddComponent<RectTransform>();
    contentRect->SetOffsetMin({ 0.0f, 0.0f });
    contentRect->SetOffsetMax({ 300.0f, 0.0f });
    ContentFit* const fit = contentObject->AddComponent<ContentFit>();
    fit->SetItemSize(30.0f);
    fit->SetSpacing(0.0f);

    const auto addRow = [&scene, contentObject](const char* const name)
    {
        GameObject* const row = scene->CreateGameObject(name);
        static_cast<void>(row->GetTransform().SetParent(&contentObject->GetTransform()));
        static_cast<void>(row->AddComponent<RectTransform>());
    };
    for (int index = 0; index < 20; ++index)
    {
        addRow("Row");
    }

    static_cast<void>(sceneManager.AddScene(std::move(scene)));

    const UILayoutSystem layout;
    layout.Synchronize(sceneManager, 800.0f, 600.0f);

    // 20개 x 30픽셀 = 600. 선언에는 0이 적혀 있고, 크기는 자식에서 나왔다.
    const bool derivedFromChildren = contentRect->GetResolvedRect().height == 600.0f;
    if (!Expect(
            derivedFromChildren,
            "content height should be derived from its children, not from its declaration"))
    {
        return false;
    }

    // 600 - 200 = 400까지만 밀린다. 그 너머를 넣어도 눌린다.
    scrollRect->SetScrollOffset({ 0.0f, 5000.0f });
    layout.Synchronize(sceneManager, 800.0f, 600.0f);
    const bool clampedToOverflow = scrollRect->GetScrollOffset().GetY() == 400.0f;

    // 밀린 만큼 내용이 올라간다. viewport의 자리는 그대로다 — 밀리는 것은 내용이지 창이 아니다.
    const bool contentMoved = contentRect->GetResolvedRect().y == -400.0f &&
        viewportRect->GetResolvedRect().y == 0.0f;

    // 같은 크기로 한 번 더 돌려도 더 밀리지 않는다. 선언을 고쳐 쓰면 프레임마다 쌓인다.
    layout.Synchronize(sceneManager, 800.0f, 600.0f);
    const bool doesNotAccumulate = contentRect->GetResolvedRect().y == -400.0f;

    if (!Expect(clampedToOverflow, "an out-of-range scroll should clamp to the overflow") ||
        !Expect(contentMoved, "scrolling should move the content and leave the viewport alone") ||
        !Expect(doesNotAccumulate, "re-running layout should not scroll further each frame"))
    {
        return false;
    }

    // 내용이 viewport보다 작아지면 밀림은 0으로 돌아간다.
    fit->SetItemSize(1.0f);
    layout.Synchronize(sceneManager, 800.0f, 600.0f);
    const bool shortContentDoesNotScroll = scrollRect->GetScrollOffset().GetY() == 0.0f &&
        contentRect->GetResolvedRect().y == 0.0f;
    return Expect(
        shortContentDoesNotScroll, "content smaller than the viewport should not scroll at all");
}

namespace
{
    /// <summary>
    /// 플랫폼 입력을 대신한다. 커서 자리와 버튼 상태를 시험이 정할 수 있어야 세 상태를 모두
    /// 실제 경로로 — 라우터를 지나 — 만들어 볼 수 있다.
    /// </summary>
    class FakePointer final : public GameEngine::Platform::IInput
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

        void SetCursor(const int x, const int y) { mState.cursor = { x, y }; }
        void SetLeftButton(const bool isDown)
        {
            mState.SetMouseButton(GameEngine::Platform::MouseButton::Left, isDown);
        }

    private:
        GameEngine::Platform::InputState mState;
    };

    /// <summary>색 두 개가 채널 하나에서 얼마나 벌어지는지다. 0..255로 환산한 최대 차이다.</summary>
    [[nodiscard]] float ChannelDistance(
        const GameEngine::Math::Color& left, const GameEngine::Math::Color& right)
    {
        const float red = std::fabs(left.r - right.r) * 255.0f;
        const float green = std::fabs(left.g - right.g) * 255.0f;
        const float blue = std::fabs(left.b - right.b) * 255.0f;
        return (std::max)(red, (std::max)(green, blue));
    }
}

/// <summary>
/// 유지 모드 UI가 포인터를 "가져갔다"고 답하는 자리와 답하지 않는 자리를 고정한다.
///
/// 이것이 툴바 클릭이 아래 패널로 새지 않는 근거다. 에디터는 한 화면에 두 UI 체계를 세운다:
/// 유지 모드 툴바가 먼저 판정하고, 그 결과를 즉시 모드 UI에 <c>SetPointerConsumedExternally</c>로
/// 넘긴다(<c>EditorShell</c>). 즉시 모드 쪽이 그 신호를 지키는지는 이미 시험이 있다. 여기서
/// 확인하는 것은 <b>그 신호 자체가 옳게 만들어지는가</b>이며, 둘이 맞물려야 계약이 닫힌다.
///
/// 그리고 이 판정은 양쪽으로 틀릴 수 있다. 버튼 위에서 가져가지 않으면 클릭이 아래 패널까지
/// 내려가 툴바를 눌렀는데 그 아래가 함께 반응한다. 반대로 빈 자리에서까지 가져가면 즉시 모드
/// UI 전체가 눌리지 않는데, 그쪽이 훨씬 나쁘다 — 화면은 멀쩡한데 아무것도 반응하지 않는다.
/// 그래서 두 방향을 함께 잰다.
/// </summary>
bool RunPointerConsumptionTests()
{
    using GameEngine::Runtime::Button;
    using GameEngine::Runtime::Canvas;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::UIEventSystem;
    using GameEngine::Runtime::UILayoutSystem;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "Toolbar");
    GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    static_cast<void>(canvasObject->AddComponent<Canvas>());

    // 툴바의 첫 버튼과 같은 모양이다: 띠의 왼쪽 위에 놓인 사각형 하나.
    GameObject* const buttonObject = scene->CreateGameObject("New Project");
    static_cast<void>(buttonObject->GetTransform().SetParent(&canvasObject->GetTransform()));
    RectTransform* const rect = buttonObject->AddComponent<RectTransform>();
    rect->SetAnchorMin({ 0.0f, 0.0f });
    rect->SetAnchorMax({ 0.0f, 0.0f });
    rect->SetOffsetMin({ 4.0f, 4.0f });
    rect->SetOffsetMax({ 100.0f, 28.0f });
    Button* const button = buttonObject->AddComponent<Button>();

    const unsigned int sceneId = sceneManager.AddScene(std::move(scene));
    if (!Expect(sceneId != 0 && button, "the toolbar scene should assemble"))
    {
        return false;
    }

    const UILayoutSystem layout;
    UIEventSystem events;
    FakePointer pointer;
    const auto step = [&]()
    {
        input.BeginFrame(pointer);
        layout.Synchronize(sceneManager, 1280.0f, 720.0f);
        return events.Synchronize(sceneManager, input);
    };

    // 버튼 한가운데. 여기서 가져가지 않으면 클릭이 아래 패널로 샌다.
    pointer.SetCursor(50, 16);
    const bool consumedOverButton = step();

    // 누르고 있는 동안에도 가져간 상태여야 한다 — 즉시 모드 쪽이 이 프레임에 판정하기 때문이다.
    pointer.SetLeftButton(true);
    const bool consumedWhilePressed = step();
    pointer.SetLeftButton(false);
    static_cast<void>(step());

    // 툴바 띠 아래의 빈 자리다. 여기서까지 가져가면 그 아래 패널들이 통째로 죽는다.
    pointer.SetCursor(400, 300);
    const bool consumedOverEmptySpace = step();

    // 눌린 채로 빈 자리에 있어도 마찬가지다.
    pointer.SetLeftButton(true);
    const bool consumedPressedOnEmptySpace = step();
    pointer.SetLeftButton(false);
    static_cast<void>(step());

    // 받지 않는 버튼은 클릭을 삼키지 않는다. 삼키면 아무 일도 하지 않는 채로 아래도 막는다 —
    // 사람에게는 화면이 얼어붙은 것으로 보인다.
    button->SetInteractable(false);
    pointer.SetCursor(50, 16);
    const bool consumedOverDisabledButton = step();

    return Expect(
            consumedOverButton, "the pointer over a button should be taken by the retained UI") &&
        Expect(
            consumedWhilePressed,
            "the pointer should stay taken while the button is held down") &&
        Expect(
            !consumedOverEmptySpace,
            "the pointer over empty space should be left for the UI underneath") &&
        Expect(
            !consumedPressedOnEmptySpace,
            "a press on empty space should be left for the UI underneath") &&
        Expect(
            !consumedOverDisabledButton,
            "a button that takes no input should not swallow the pointer");
}

/// <summary>
/// SetInteractable에 같은 값을 다시 전달해도 이번 프레임의 클릭을 잃지 않아야 한다.
/// 매 프레임 상태를 동기화하는 호출자는 같은 값을 반복할 수 있다.
/// 비활성화할 때는 누름 상태를 지워 입력을 받지 않는 버튼이 눌린 채 남지 않게 한다.
/// </summary>
bool RunButtonInteractableClickTests()
{
    using GameEngine::Runtime::Button;
    using GameEngine::Runtime::Canvas;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::UIEventSystem;
    using GameEngine::Runtime::UILayoutSystem;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "Buttons");
    GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    static_cast<void>(canvasObject->AddComponent<Canvas>());

    GameObject* const buttonObject = scene->CreateGameObject("Command");
    static_cast<void>(buttonObject->GetTransform().SetParent(&canvasObject->GetTransform()));
    RectTransform* const rect = buttonObject->AddComponent<RectTransform>();
    rect->SetAnchorMin({ 0.0f, 0.0f });
    rect->SetAnchorMax({ 0.0f, 0.0f });
    rect->SetOffsetMin({ 0.0f, 0.0f });
    rect->SetOffsetMax({ 120.0f, 40.0f });
    Button* const button = buttonObject->AddComponent<Button>();

    const unsigned int sceneId = sceneManager.AddScene(std::move(scene));
    if (!Expect(sceneId != 0 && button, "the button scene should assemble"))
    {
        return false;
    }

    const UILayoutSystem layout;
    UIEventSystem events;
    FakePointer pointer;
    // 툴바가 하는 그대로다: 프레임마다 조건을 다시 계산해 버튼에 알리고, 그 뒤에 클릭을 읽는다.
    const auto stepKeepingItEnabled = [&]()
    {
        input.BeginFrame(pointer);
        layout.Synchronize(sceneManager, 800.0f, 600.0f);
        static_cast<void>(events.Synchronize(sceneManager, input));
        button->SetInteractable(true);
        return button->WasClickedThisFrame();
    };

    pointer.SetCursor(60, 20);
    static_cast<void>(stepKeepingItEnabled());
    pointer.SetLeftButton(true);
    static_cast<void>(stepKeepingItEnabled());
    pointer.SetLeftButton(false);
    const bool clickSurvivesTheUpdate = stepKeepingItEnabled();

    // 끄는 쪽은 여전히 지운다. 받지 않게 된 버튼이 눌린 채로 남으면 그 클릭이 나중에 도착한다.
    pointer.SetLeftButton(true);
    input.BeginFrame(pointer);
    layout.Synchronize(sceneManager, 800.0f, 600.0f);
    static_cast<void>(events.Synchronize(sceneManager, input));
    pointer.SetLeftButton(false);
    input.BeginFrame(pointer);
    layout.Synchronize(sceneManager, 800.0f, 600.0f);
    static_cast<void>(events.Synchronize(sceneManager, input));
    button->SetInteractable(false);
    const bool disablingClearsTheClick = !button->WasClickedThisFrame();

    return Expect(
            clickSurvivesTheUpdate,
            "a button told it is still interactable should keep this frame's click") &&
        Expect(
            disablingClearsTheClick,
            "a button that stops taking input should not keep a click to deliver later");
}

bool RunButtonStateContrastTests()
{
    using GameEngine::Runtime::Button;
    using GameEngine::Runtime::Canvas;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::SpriteRenderer;
    using GameEngine::Runtime::UIEventSystem;
    using GameEngine::Runtime::UILayoutSystem;

    std::cout << "running button state contrast tests\n";
    bool passed = true;

    // 사람 눈이 평평한 색 두 장을 다르다고 보려면 채널 차이가 얼마나 나야 하는가. 채널 차이 3은
    // 부족하다: 채움색(11,12,13)과 배경(16,17,20)은 "다른 값"이지만 사람 눈에는 같아 보이고, 그
    // 차이가 3이다. 여기서는 그 네 배인 12를 문턱으로 둔다. 상태 표시는 알아채라고 있는 것이므로
    // 겨우 보이는 정도로는 부족하고, 흔한 UI 팔레트가 호버에 주는 밝기 단차(약 5%, 255의 13)와도
    // 같은 자리다.
    constexpr float VisibleChannelDistance = 12.0f;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };
    auto scene = std::make_unique<GameEngine::Runtime::Scene>(context, "ButtonContrast");

    GameEngine::Runtime::GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    GameEngine::Runtime::GameObject* const buttonObject = scene->CreateGameObject("Button");
    if (!Expect(canvasObject && buttonObject, "the contrast scene should assemble"))
    {
        return false;
    }
    static_cast<void>(canvasObject->AddComponent<Canvas>());

    RectTransform* const rect = buttonObject->AddComponent<RectTransform>();
    rect->SetOffsetMin({ 100.0f, 100.0f });
    rect->SetOffsetMax({ 200.0f, 140.0f });
    // 색이 실제로 실리는 곳이다. 버튼은 자기 그림을 만들지 않고 같은 오브젝트의 스프라이트에
    // 색을 입힌다 — 그 색이 draw의 틴트가 되고, 틴트가 화면의 픽셀을 정한다.
    SpriteRenderer* const sprite = buttonObject->AddComponent<SpriteRenderer>();
    Button* const button = buttonObject->AddComponent<Button>();
    static_cast<void>(buttonObject->GetTransform().SetParent(&canvasObject->GetTransform()));
    if (!Expect(
            sceneManager.AddScene(std::move(scene)) != 0 && rect && sprite && button,
            "the contrast scene should be added"))
    {
        return false;
    }

    FakePointer pointer;
    const UILayoutSystem layout;
    UIEventSystem events;
    const auto step = [&](const int cursorX, const int cursorY, const bool pressed)
    {
        pointer.SetCursor(cursorX, cursorY);
        pointer.SetLeftButton(pressed);
        input.BeginFrame(pointer);
        layout.Synchronize(sceneManager, 800.0f, 600.0f);
        static_cast<void>(events.Synchronize(sceneManager, input));
        return sprite->GetColor();
    };

    // 네 상태를 실제 경로로 만든다: 밖 → 위 → 위에서 누름 → 받지 않음.
    const GameEngine::Math::Color normal = step(10, 10, false);
    const GameEngine::Math::Color hovered = step(150, 120, false);
    const GameEngine::Math::Color pressed = step(150, 120, true);
    // 네 번째 상태다. 받는 상태 셋만 재고 받지 않는 상태를 빼면, 거리가 좁은 것이 아니라 0인
    // 종류의 결함은 문턱을 아무리 낮춰도 잡히지 않는다.
    button->SetInteractable(false);
    const GameEngine::Math::Color disabled = step(10, 10, false);
    button->SetInteractable(true);

    // 버튼 팔레트의 색은 다른 상태, 아래 면, 글자와의 관계로 검사한다.
    // R1: 같은 버튼의 네 상태 색은 서로 구별되어야 한다.
    // R2: 버튼은 배경 패널과 구별되어야 한다.
    // R3: 버튼 위 글자는 읽을 수 있어야 한다.
    // R4: 스프라이트를 사용하는 버튼은 그림에 곱해질 자체 팔레트를 지정한다.
    // R5: 다른 위젯과의 구분도 고려한다.
    // R6: 같은 화면의 즉시 모드와 유지 모드 버튼은 일관된 색을 사용해야 한다.
    // R7: 네 색이 불투명하므로 배경과의 알파 혼합은 검사하지 않는다.
    // R8: 포커스 표시가 정의되지 않은 버튼은 포커스 색을 검사하지 않는다.
    struct Relation
    {
        const char* name;
        float distance;
        enum class Rule
        {
            Required,  // 문턱을 넘어야 한다.
            Exempt,    // 명시한 사유로 문턱 충족을 요구하지 않는 관계다.
            Pending,   // 문턱 아래인데 아직 요구로 세울 수 없는 것. 지금은 비어 있다.
        } rule;
        const char* note;
    };

    const auto& panel = GameEditor::PanelColor;
    const auto& header = GameEditor::HeaderColor;
    const auto& label = GameEditor::TextColor;
    const auto& dimLabel = GameEditor::DimTextColor;
    // 비활성 버튼이 설 수 있는 면 중 <b>가장 어두운 것</b>이다.
    //
    // 툴바 띠는 HeaderColor에 9-슬라이스 그림을 곱해 그려진다. 그림의 채움은 흰색이라 띠의
    // 대부분은 HeaderColor 그대로 43,46,51이고, 곱이 색을 낮추는 곳은 테두리 1px 선뿐이다.
    // 그 선의 텍셀이 190이고, 선형 공간에서 곱해 다시 sRGB로 인코딩하면 29,31,35이 된다 —
    // 버튼이 띠의 가장자리에 앉을 때 그 옆에 오는 색이 이것이라 여기에 요구를 건다.
    //
    // 둘 중 어두운 쪽에 요구를 거는 이유는 방향이 한쪽으로만 성립하기 때문이다: 어두운 배경을
    // 통과한 값은 밝은 배경도 통과하지만 그 반대는 아니다.
    constexpr GameEngine::Math::Color measuredToolbarBand{
        29.0f / 255.0f, 31.0f / 255.0f, 35.0f / 255.0f, 1.0f };

    const std::vector<Relation> relations{
        // R1 상태 ↔ 상태.
        { "R1 normal-hovered", ChannelDistance(normal, hovered), Relation::Rule::Required, "" },
        { "R1 hovered-pressed", ChannelDistance(hovered, pressed), Relation::Rule::Required, "" },
        { "R1 normal-pressed", ChannelDistance(normal, pressed), Relation::Rule::Required, "" },
        { "R1 normal-disabled", ChannelDistance(normal, disabled), Relation::Rule::Required,
          "" },
        { "R1 hovered-disabled", ChannelDistance(hovered, disabled), Relation::Rule::Required,
          "" },
        { "R1 pressed-disabled", ChannelDistance(pressed, disabled), Relation::Rule::Exempt,
          "one button cannot be both pressed and disabled" },
        // R2 버튼 ↔ 아래 면. 편집기에서 버튼이 실제로 앉는 두 면이 판과 띠다.
        { "R2 normal-on-panel", ChannelDistance(normal, panel), Relation::Rule::Required, "" },
        { "R2 hovered-on-panel", ChannelDistance(hovered, panel), Relation::Rule::Required, "" },
        { "R2 pressed-on-panel", ChannelDistance(pressed, panel), Relation::Rule::Exempt,
          "pressing is momentary and the cursor is on it -- but a tool scanning the screen"
          " cannot see it either: this is an exemption for people, not for tools" },
        { "R2 disabled-on-panel", ChannelDistance(disabled, panel), Relation::Rule::Required, "" },
        { "R2 normal-on-header", ChannelDistance(normal, header), Relation::Rule::Required, "" },
        { "R2 hovered-on-header", ChannelDistance(hovered, header), Relation::Rule::Required, "" },
        { "R2 pressed-on-header", ChannelDistance(pressed, header), Relation::Rule::Exempt,
          "same as above: momentary for a person, invisible to a tool" },
        { "R2 disabled-on-header", ChannelDistance(disabled, header), Relation::Rule::Required,
          "" },
        { "R2 disabled-on-measured-band", ChannelDistance(disabled, measuredToolbarBand),
          Relation::Rule::Required, "" },
        // R3 버튼 ↔ 글자. 누를 수 있는 버튼은 밝은 글자, 누를 수 없는 쪽은 흐린 글자다.
        { "R3 label-on-normal", ChannelDistance(label, normal), Relation::Rule::Required, "" },
        { "R3 label-on-hovered", ChannelDistance(label, hovered), Relation::Rule::Required, "" },
        { "R3 label-on-pressed", ChannelDistance(label, pressed), Relation::Rule::Required, "" },
        { "R3 dim-label-on-disabled", ChannelDistance(dimLabel, disabled),
          Relation::Rule::Required, "" },
        // R5 버튼 ↔ 다른 위젯. 필드는 즉시 모드 상수라 여기서 직접 볼 수 없어 수를 적어 둔다.
        { "R5 disabled-vs-text-field", ChannelDistance(disabled,
              GameEngine::Math::Color{ 0.13f, 0.14f, 0.16f, 1.0f }),
          Relation::Rule::Required, "" },
    };

    std::vector<std::string> below;
    for (const Relation& relation : relations)
    {
        std::cout << "  " << relation.name << " = " << relation.distance;
        if (relation.rule == Relation::Rule::Exempt)
        {
            std::cout << " (not required: " << relation.note << ")";
        }
        std::cout << "\n";
        if (relation.rule == Relation::Rule::Required)
        {
            const std::string message =
                std::string{ relation.name } + " should clear the visible threshold";
            passed = Expect(
                relation.distance >= VisibleChannelDistance,
                message.c_str()) && passed;
        }
        if (relation.rule != Relation::Rule::Exempt && relation.distance < VisibleChannelDistance)
        {
            below.emplace_back(relation.name);
        }
    }

    // 문턱 아래인 색 관계가 사유 없이 허용되지 않는지 검사한다.
    // 명시적 면제를 제외한 모든 관계가 문턱을 만족해야 하며 면제를 추가할 때는 사유를 기록해야 한다.
    const std::vector<std::string> knownBelow{};
    std::cout << "  below the threshold and not exempt: " << below.size() << " relations\n";
    passed = Expect(
        below == knownBelow,
        "the set of relations below the threshold should be exactly the known list") && passed;

    // R4. 이 값들은 배수가 아니라 색이다. 그래서 그림 위에 곱해지면 오히려 더 좁아진다 —
    // 어두운 색을 어두운 그림에 곱하기 때문이다. 아래 수는 "팔레트가 나쁘다"는 뜻이
    // 아니라 <b>그 조합이 틀렸다</b>는 뜻이다: 이 팔레트는 그림 없는 단색 사각형 위에서
    // 쓰이고, 그때 틴트가 곧 화면의 색이라 위에서 잰 거리가 그대로 화면의 거리다.
    // 그림을 깐 버튼은 자기 팔레트를 따로 정해야 하므로 여기서 요구하지 않는다.
    constexpr float DarkSpriteLevel = 0.08f;
    std::cout << "  R4 on a sprite at " << DarkSpriteLevel * 255.0f
              << "/255 the same palette narrows to "
              << ChannelDistance(normal, hovered) * DarkSpriteLevel << ", "
              << ChannelDistance(hovered, pressed) * DarkSpriteLevel << ", "
              << ChannelDistance(normal, pressed) * DarkSpriteLevel
              << " (not required: a button with a picture must pick its own palette)\n";
    // R6. 즉시 모드 상수는 UIContext.cpp의 익명 이름공간에 있어 여기서 볼 수 없다. 그래서
    // 두 구현이 같은 색인지 시험이 확인하지 못한다 — 한쪽이 값을 베껴 두고 팔레트가 움직이는
    // 동안 뒤처지면 `disabled == normal` 같은 상태가 된다.
    std::cout << "  R6 immediate-mode constants are not visible to this test:"
                 " the two implementations are kept equal by hand\n";
    // R8. 키보드 포커스에는 색이 없다. 관계가 아니라 값이 빠져 있다.
    std::cout << "  R8 there is no focus colour at all, so no relation can be measured\n";

    return passed;
}

bool RunButtonSurfaceTests()
{
    using GameEngine::Runtime::Button;
    using GameEngine::Runtime::Canvas;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::SpriteRenderer;

    std::cout << "running button surface tests\n";
    bool passed = true;

    // 앞의 대비 시험이 읽는 것은 스프라이트의 색이고, 사람이 보는 것은 프레임에 실린 틴트다.
    // 그 사이 한 칸 — 상태 색이 draw까지 도달하는가 — 을 여기서 닫는다. 버튼이 반응하는데
    // 보이지 않는 고장이 생기는 자리가 그 칸이다.
    GameEngine::Runtime::Game game{ nullptr, nullptr };
    auto scene = std::make_unique<GameEngine::Runtime::Scene>(
        game.GetRuntimeContext(), "ButtonSurface");
    GameEngine::Runtime::GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    GameEngine::Runtime::GameObject* const buttonObject = scene->CreateGameObject("Button");
    if (!Expect(canvasObject && buttonObject, "the button surface scene should assemble"))
    {
        return false;
    }
    static_cast<void>(canvasObject->AddComponent<Canvas>());

    RectTransform* const rect = buttonObject->AddComponent<RectTransform>();
    rect->SetOffsetMin({ 100.0f, 100.0f });
    rect->SetOffsetMax({ 220.0f, 140.0f });
    // 그림을 주지 않는다. 버튼의 바탕은 단색 사각형이고, 그것이 UI에서 가장 흔한 모습이다.
    SpriteRenderer* const sprite = buttonObject->AddComponent<SpriteRenderer>();
    sprite->SetSpace(SpriteRenderer::Space::Screen);
    // 버튼은 붙어 있기만 하면 된다. 상태를 만드는 것은 라우터이고, 그 결과를 읽는 자리는
    // 스프라이트가 아니라 프레임이다.
    static_cast<void>(buttonObject->AddComponent<Button>());
    static_cast<void>(buttonObject->GetTransform().SetParent(&canvasObject->GetTransform()));
    if (!Expect(game.AddScene(std::move(scene)) != 0, "the button surface scene should be added"))
    {
        return false;
    }
    game.SetRenderSurfaceSize(800.0f, 600.0f);

    FakePointer pointer;
    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer)
    {
        std::cout << "  button surface tests skipped: no bundled font on this machine\n";
        return true;
    }
    GameEngine::SceneRendering::SceneRenderPass frontend{
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(std::move(rasterizer)) };
    const auto tintOnScreen = [&](const int cursorX, const int cursorY, const bool pressed)
    {
        pointer.SetCursor(cursorX, cursorY);
        pointer.SetLeftButton(pressed);
        game.GetInput().BeginFrame(pointer);
        game.Update(0.0f);

        GameEngine::Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ 800, 600 });
        frontend.Collect(game, builder);
        const GameEngine::Rendering::RenderFrame frame = std::move(builder).Build();
        const std::vector<const GameEngine::Rendering::SpriteDraw*> drawn =
            frame.GetDraws<GameEngine::Rendering::SpriteDraw>(
                GameEngine::Rendering::RenderPass::Overlay);
        return drawn.empty() ? GameEngine::Math::Color{ 0.0f, 0.0f, 0.0f, 0.0f }
                             : drawn.front()->tint;
    };

    const GameEngine::Math::Color normal = tintOnScreen(10, 10, false);
    const GameEngine::Math::Color hovered = tintOnScreen(150, 120, false);
    const GameEngine::Math::Color pressed = tintOnScreen(150, 120, true);

    passed = Expect(
        normal.a > 0.0f && hovered.a > 0.0f && pressed.a > 0.0f,
        "a button with no sprite should still reach the frame in every state") && passed;
    // 프레임까지 온 세 색이 서로 구분된다. 여기까지 오면 남은 것은 틴트가 픽셀이 되는 구간뿐이고,
    // 그것은 두 백엔드의 이미지 비교가 이미 지킨다.
    passed = Expect(
        ChannelDistance(normal, hovered) >= 12.0f &&
            ChannelDistance(hovered, pressed) >= 12.0f &&
            ChannelDistance(normal, pressed) >= 12.0f,
        "the three states should still be far apart once they reach the frame") && passed;

    return passed;
}

bool RunDropdownTests()
{
    using GameEngine::Runtime::Dropdown;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::TextRenderer;
    using GameEngine::Runtime::UIEventSystem;
    using GameEngine::Runtime::UILayoutSystem;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "Dropdown");
    GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    static_cast<void>(canvasObject->AddComponent<GameEngine::Runtime::Canvas>());

    // 머리 칸은 (0,0)에서 120x20이다. 항목 줄은 그 아래로 같은 높이씩 이어진다.
    GameObject* const object = scene->CreateGameObject("Choice");
    static_cast<void>(object->GetTransform().SetParent(&canvasObject->GetTransform()));
    RectTransform* const rect = object->AddComponent<RectTransform>();
    rect->SetAnchorMin({ 0.0f, 0.0f });
    rect->SetAnchorMax({ 0.0f, 0.0f });
    rect->SetOffsetMin({ 0.0f, 0.0f });
    rect->SetOffsetMax({ 120.0f, 20.0f });
    Dropdown* const dropdown = object->AddComponent<Dropdown>();
    TextRenderer* const text = object->AddComponent<TextRenderer>();
    dropdown->SetOptions({ "Alpha", "Beta", "Gamma" });

    const unsigned int sceneId = sceneManager.AddScene(std::move(scene));
    if (!Expect(sceneId != 0 && dropdown && text, "the dropdown scene should assemble"))
    {
        return false;
    }

    const UILayoutSystem layout;
    UIEventSystem events;
    const auto step = [&layout, &events, &sceneManager, &input]()
    {
        layout.Synchronize(sceneManager, 800.0f, 600.0f);
        return events.Synchronize(sceneManager, input);
    };

    // 커서는 원점이고 그 자리는 머리 칸 안이다. 접혀 있으면 머리 칸만 이 요소다.
    const bool consumed = step();
    const bool hoveredWhenClosed = consumed && dropdown->IsHovered();
    const bool closedCoversOnlyHeader = dropdown->Covers(10.0f, 10.0f) &&
        !dropdown->Covers(10.0f, 30.0f) && !dropdown->OptionAt(10.0f, 30.0f).has_value();

    // 값은 인덱스이고, 글자는 같은 오브젝트의 TextRenderer가 보인다.
    const bool startsEmpty = dropdown->GetValue() == -1 && text->GetText().empty();
    dropdown->SetValue(1);
    const bool displaysChoice = dropdown->GetValue() == 1 && text->GetText() == "Beta";
    dropdown->SetValue(7);
    const bool rejectsOutOfRange = dropdown->GetValue() == -1 && text->GetText().empty();
    dropdown->SetValue(1);

    // 펼치면 항목 줄들이 머리 칸 아래로 붙는다: 세 줄이면 y 20에서 80 앞까지다.
    dropdown->Open();
    const bool openCoversList = dropdown->IsOpen() && dropdown->Covers(10.0f, 30.0f) &&
        dropdown->Covers(10.0f, 79.0f) && !dropdown->Covers(10.0f, 80.0f);
    const bool optionIndices = dropdown->OptionAt(10.0f, 30.0f) == 0 &&
        dropdown->OptionAt(10.0f, 50.0f) == 1 && dropdown->OptionAt(10.0f, 70.0f) == 2 &&
        !dropdown->OptionAt(10.0f, 10.0f).has_value() &&
        !dropdown->OptionAt(200.0f, 30.0f).has_value();

    // 줄 높이를 따로 정하면 그 높이로 나뉜다.
    dropdown->SetOptionHeight(10.0f);
    const bool customRowHeight = dropdown->OptionAt(10.0f, 25.0f) == 0 &&
        dropdown->OptionAt(10.0f, 45.0f) == 2 && !dropdown->OptionAt(10.0f, 50.0f).has_value();
    dropdown->SetOptionHeight(0.0f);

    // 아무도 누르지 않은 프레임은 펼침을 건드리지 않는다.
    static_cast<void>(step());
    const bool staysOpen = dropdown->IsOpen() && !dropdown->WasValueChangedThisFrame();

    // 받지 않게 되면 접히고 커서도 받지 않는다.
    dropdown->SetInteractable(false);
    const bool closesWhenDisabled = !dropdown->IsOpen();
    static_cast<void>(step());
    const bool ignoredWhenDisabled = !dropdown->IsHovered();
    dropdown->SetInteractable(true);

    // 목록이 줄어 값이 범위를 벗어나면 값이 없어진다.
    dropdown->SetOptions({ "Solo" });
    const bool clampsToNewList = dropdown->GetValue() == -1 && text->GetText().empty();

    return Expect(hoveredWhenClosed, "the cursor on the header should hover the dropdown") &&
        Expect(closedCoversOnlyHeader, "a closed dropdown should cover only its header") &&
        Expect(startsEmpty, "a new dropdown should have no value and show nothing") &&
        Expect(displaysChoice, "the selected option should be shown by the text renderer") &&
        Expect(rejectsOutOfRange, "a value outside the options should leave nothing selected") &&
        Expect(openCoversList, "an open dropdown should cover its option rows") &&
        Expect(optionIndices, "each option row should map to its index") &&
        Expect(customRowHeight, "a custom option height should size the rows") &&
        Expect(staysOpen, "a frame with no press should leave the list open") &&
        Expect(closesWhenDisabled && ignoredWhenDisabled, "disabling should close the list and drop the cursor") &&
        Expect(clampsToNewList, "a value past the new option count should be cleared");
}

bool RunSameFrameClickTests()
{
    using GameEngine::Runtime::Button;
    using GameEngine::Runtime::Canvas;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::UIEventSystem;
    using GameEngine::Runtime::UILayoutSystem;

    std::cout << "running same-frame click tests\n";
    bool passed = true;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };
    auto scene = std::make_unique<GameEngine::Runtime::Scene>(context, "SameFrameClick");

    GameEngine::Runtime::GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    GameEngine::Runtime::GameObject* const buttonObject = scene->CreateGameObject("Button");
    if (!Expect(canvasObject && buttonObject, "the same-frame click scene should assemble"))
    {
        return false;
    }
    static_cast<void>(canvasObject->AddComponent<Canvas>());
    RectTransform* const rect = buttonObject->AddComponent<RectTransform>();
    rect->SetOffsetMin({ 100.0f, 100.0f });
    rect->SetOffsetMax({ 200.0f, 140.0f });
    Button* const button = buttonObject->AddComponent<Button>();
    static_cast<void>(buttonObject->GetTransform().SetParent(&canvasObject->GetTransform()));
    if (!Expect(sceneManager.AddScene(std::move(scene)) != 0 && button, "the scene should be added"))
    {
        return false;
    }

    FakePointer pointer;
    const UILayoutSystem layout;
    UIEventSystem events;
    pointer.SetCursor(150, 120);
    const auto frame = [&]
    {
        input.BeginFrame(pointer);
        layout.Synchronize(sceneManager, 800.0f, 600.0f);
        static_cast<void>(events.Synchronize(sceneManager, input));
        return button->WasClickedThisFrame();
    };

    // 누름과 뗌이 프레임을 넘어가는 클릭이다. 사람이 천천히 누르면 이 모양이다.
    pointer.SetLeftButton(false);
    static_cast<void>(frame());
    pointer.SetLeftButton(true);
    static_cast<void>(frame());
    pointer.SetLeftButton(false);
    const int acrossFrames = frame() ? 1 : 0;

    // 누름과 뗌이 한 프레임 안에서 끝나는 클릭이다. 빠른 클릭과 합성 입력이 이 모양이고,
    // 표본을 두 번 비교하는 쪽에는 버튼이 두 번 다 올라와 있는 것으로만 보인다.
    pointer.SetLeftButton(false);
    static_cast<void>(frame());
    pointer.SetLeftButton(true);
    pointer.SetLeftButton(false);
    const int withinOneFrame = frame() ? 1 : 0;

    // 한 프레임 안에 클릭이 <b>둘</b> 들어오는 경우다. 누적은 둘을 세지만 이 층의 질문은
    // "눌렸는가"라 참/거짓 하나이므로, 프레임당 하나만 전달된다. 더블클릭은 400ms 안의 두
    // 클릭이라 보통 서로 다른 프레임에 놓이고 영향받지 않는다 — 두 클릭이 한 프레임에
    // 겹칠 만큼 빠른 경우에만 하나로 합쳐진다.
    pointer.SetLeftButton(false);
    static_cast<void>(frame());
    pointer.SetLeftButton(true);
    pointer.SetLeftButton(false);
    pointer.SetLeftButton(true);
    pointer.SetLeftButton(false);
    const int twoWithinOneFrame = frame() ? 1 : 0;

    std::cout << "  clicks seen: across frames=" << acrossFrames
              << ", within one frame=" << withinOneFrame << " (of 1 each); two clicks"
              << " inside one frame arrive as " << twoWithinOneFrame << " of 2\n";

    passed = Expect(acrossFrames == 1, "a click spread over two frames should reach the button") &&
        passed;
    passed = Expect(
        withinOneFrame == 1, "a click that begins and ends within one frame should also reach it") &&
        passed;

    return passed;
}

bool RunSameFrameKeyTests()
{
    std::cout << "running same-frame key tests\n";

    GameEngine::Runtime::Input input;
    FakePointer source;

    // 프레임을 넘겨 누르고 떼는 키다. 사람이 천천히 누르면 이 모양이다.
    source.SetKey(GameEngine::Platform::Key::Z, false);
    input.BeginFrame(source);
    source.SetKey(GameEngine::Platform::Key::Z, true);
    input.BeginFrame(source);
    const int acrossFrames = input.GetKeyDown(GameEngine::Platform::Key::Z) ? 1 : 0;
    source.SetKey(GameEngine::Platform::Key::Z, false);
    input.BeginFrame(source);

    // 한 프레임 안에서 눌렸다 떼어진 키다. 프레임이 길어지는 순간 — 로딩, 에셋 훑기 — 의
    // 단축키가 이 모양이고, 표본을 두 번 비교하는 쪽에는 두 번 다 올라와 있는 것으로만 보인다.
    source.SetKey(GameEngine::Platform::Key::Z, true);
    source.SetKey(GameEngine::Platform::Key::Z, false);
    input.BeginFrame(source);
    const int withinOneFrame = input.GetKeyDown(GameEngine::Platform::Key::Z) ? 1 : 0;

    std::cout << "  key presses seen: across frames=" << acrossFrames
              << ", within one frame=" << withinOneFrame << " (of 1 each)\n";

    bool passed = Expect(acrossFrames == 1, "a keypress spread over two frames should be seen");
    passed = Expect(
        withinOneFrame == 1,
        "a keypress that begins and ends within one frame should also be seen") && passed;
    return passed;
}

static const TestSupport::Registration gUIPointerRouterTests{
    "UIEvent", "UI pointer router tests should pass", RunUIPointerRouterTests };

static const TestSupport::Registration gUIEventSystemTests{
    "UIEvent", "UI event system tests should pass", RunUIEventSystemTests };

static const TestSupport::Registration gHierarchyOrderBeatsKindTests{
    "UIEvent", "hierarchy order should beat element kind", RunHierarchyOrderBeatsKindTests };

static const TestSupport::Registration gPointerConsumptionTests{
    "UIEvent", "pointer consumption tests should pass", RunPointerConsumptionTests };

static const TestSupport::Registration gButtonInteractableClickTests{
    "UIEvent", "button interactable click tests should pass", RunButtonInteractableClickTests };

static const TestSupport::Registration gScrollRectTests{
    "UIEvent", "scroll rect tests should pass", RunScrollRectTests };

static const TestSupport::Registration gButtonStateContrastTests{
    "UIEvent", "button state contrast tests should pass", RunButtonStateContrastTests };

static const TestSupport::Registration gSameFrameClickTests{
    "UIEvent", "same-frame click tests should pass", RunSameFrameClickTests };

static const TestSupport::Registration gSameFrameKeyTests{
    "UIEvent", "same-frame key tests should pass", RunSameFrameKeyTests };

static const TestSupport::Registration gButtonSurfaceTests{
    "UIEvent", "button surface tests should pass", RunButtonSurfaceTests };

static const TestSupport::Registration gDropdownTests{
    "UIEvent", "dropdown tests should pass", RunDropdownTests };
