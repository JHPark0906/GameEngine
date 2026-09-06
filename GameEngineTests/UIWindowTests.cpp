#include <iostream>
#include <memory>
#include <string>

#include "../GameEngine/Runtime/Button.h"
#include "../GameEngine/Runtime/Canvas.h"
#include "../GameEngine/Runtime/GameObject.h"
#include "../GameEngine/Runtime/Input.h"
#include "../GameEngine/Runtime/ObjectRegistry.h"
#include "../GameEngine/Runtime/RectTransform.h"
#include "../GameEngine/Runtime/RuntimeContext.h"
#include "../GameEngine/Runtime/Scene.h"
#include "../GameEngine/Runtime/SceneManager.h"
#include "../GameEngine/Runtime/Transform.h"
#include "../GameEngine/Runtime/UIEventSystem.h"
#include "../GameEngine/Runtime/UILayoutSystem.h"
#include "../GameEngine/Runtime/UIWindow.h"

#include "UIWindowTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEngine::Runtime::Button;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::Transform;
    using GameEngine::Runtime::UIWindow;

    /// <summary>
    /// 창 하나를 세운다. 사각형은 논리 좌표로 주며, 커서는 원점에 있으므로 원점을 덮는지 여부가
    /// 곧 "커서 아래 있는가"이다.
    /// </summary>
    GameObject* AddWindow(
        Scene& scene, GameObject& canvas, const char* const name, const float left,
        const float top, const float right, const float bottom)
    {
        GameObject* const object = scene.CreateGameObject(name);
        static_cast<void>(object->GetTransform().SetParent(&canvas.GetTransform()));
        RectTransform* const rect = object->AddComponent<RectTransform>();
        rect->SetAnchorMin({ 0.0f, 0.0f });
        rect->SetAnchorMax({ 0.0f, 0.0f });
        rect->SetOffsetMin({ left, top });
        rect->SetOffsetMax({ right, bottom });
        static_cast<void>(object->AddComponent<UIWindow>());
        return object;
    }

    /// <summary>창 안에 버튼 하나를 놓는다. 자리는 창과 같아, 창이 곧 그 버튼의 자리다.</summary>
    Button* AddButtonIn(
        Scene& scene, GameObject& window, const char* const name, const float left,
        const float top, const float right, const float bottom)
    {
        GameObject* const object = scene.CreateGameObject(name);
        static_cast<void>(object->GetTransform().SetParent(&window.GetTransform()));
        RectTransform* const rect = object->AddComponent<RectTransform>();
        rect->SetAnchorMin({ 0.0f, 0.0f });
        rect->SetAnchorMax({ 0.0f, 0.0f });
        rect->SetOffsetMin({ left, top });
        rect->SetOffsetMax({ right, bottom });
        return object->AddComponent<Button>();
    }
}

bool RunUIWindowOcclusionTests()
{
    using GameEngine::Runtime::UIEventSystem;
    using GameEngine::Runtime::UILayoutSystem;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "Windows");
    GameObject* const canvas = scene->CreateGameObject("Canvas");
    static_cast<void>(canvas->AddComponent<GameEngine::Runtime::Canvas>());

    // 아래 창에는 원점을 덮는 버튼이 있다. 위 창은 같은 자리를 덮지만 그 자리에 요소가 없다 —
    // 배경뿐인 부분이다. 요소만 훑는 판정은 여기서 아래 버튼을 고르고, 그것이 이 시험이 막는
    // 증상이다: 화면에서 가려져 보이지 않는 버튼이 눌린다.
    GameObject* const lowerWindow = AddWindow(*scene, *canvas, "Lower", 0.0f, 0.0f, 200.0f, 100.0f);
    Button* const covered = AddButtonIn(*scene, *lowerWindow, "Covered", 0.0f, 0.0f, 80.0f, 40.0f);
    GameObject* const upperWindow = AddWindow(*scene, *canvas, "Upper", 0.0f, 0.0f, 200.0f, 100.0f);
    // 위 창의 버튼은 커서에서 비켜 있다 — 위 창의 그 자리는 배경이다.
    Button* const elsewhere =
        AddButtonIn(*scene, *upperWindow, "Elsewhere", 120.0f, 60.0f, 200.0f, 100.0f);

    if (!Expect(
            sceneManager.AddScene(std::move(scene)) != 0 && covered && elsewhere,
            "the window scene should assemble"))
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

    static_cast<void>(step());
    const bool coveredStaysQuiet = !covered->IsHovered();

    // 위 창을 끄면 가릴 것이 없어지고, 아래 버튼이 다시 커서를 받는다. 가림이 판정을 영구히
    // 죽이는 것이 아니라 그 프레임의 배치에서 나온다는 것을 이 방향이 고정한다.
    upperWindow->SetActive(false);
    static_cast<void>(step());
    const bool coveredComesBack = covered->IsHovered();

    return Expect(
            coveredStaysQuiet,
            "a button under a higher window's empty background should not take the cursor") &&
        Expect(
            coveredComesBack,
            "hiding the higher window should give the button beneath it the cursor again");
}

bool RunUIWindowRaiseTests()
{
    using GameEngine::Runtime::UIEventSystem;
    using GameEngine::Runtime::UILayoutSystem;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "RaiseAndModal");
    GameObject* const canvas = scene->CreateGameObject("Canvas");
    static_cast<void>(canvas->AddComponent<GameEngine::Runtime::Canvas>());

    // 같은 자리에 포갠 창 둘. 각자 원점을 덮는 버튼을 하나씩 가진다.
    GameObject* const first = AddWindow(*scene, *canvas, "First", 0.0f, 0.0f, 200.0f, 100.0f);
    Button* const firstButton = AddButtonIn(*scene, *first, "FirstButton", 0.0f, 0.0f, 80.0f, 40.0f);
    GameObject* const second = AddWindow(*scene, *canvas, "Second", 0.0f, 0.0f, 200.0f, 100.0f);
    Button* const secondButton =
        AddButtonIn(*scene, *second, "SecondButton", 0.0f, 0.0f, 80.0f, 40.0f);

    if (!Expect(
            sceneManager.AddScene(std::move(scene)) != 0 && firstButton && secondButton,
            "the stacked window scene should assemble"))
    {
        return false;
    }

    const UILayoutSystem layout;
    UIEventSystem events;
    const auto step = [&layout, &events, &sceneManager, &input]()
    {
        layout.Synchronize(sceneManager, 800.0f, 600.0f);
        static_cast<void>(events.Synchronize(sceneManager, input));
    };

    step();
    const bool laterWindowWins = secondButton->IsHovered() && !firstButton->IsHovered();

    // 앞 창을 맨 뒤 형제로 옮기는 것이 "앞으로 가져오기"다. 그리는 순서도 같은 순서이므로,
    // 이것 하나로 보이는 것과 눌리는 것이 함께 바뀐다.
    first->GetTransform().SetAsLastSibling();
    step();
    const bool raisedWindowWins = firstButton->IsHovered() && !secondButton->IsHovered();

    // 모달은 다른 층의 규칙이다: 지금 아래에 있는 second를 모달로 만들면, 위에 있는 first의
    // 버튼도 후보에서 빠진다. 순서가 아니라 범위가 답을 정하는 자리다.
    second->GetComponent<UIWindow>()->SetModal(true);
    step();
    const bool modalTakesEverything = secondButton->IsHovered() && !firstButton->IsHovered();

    second->GetComponent<UIWindow>()->SetModal(false);
    step();
    const bool modalReleases = firstButton->IsHovered() && !secondButton->IsHovered();

    return Expect(
            laterWindowWins, "of two stacked windows the later declared one should take the cursor") &&
        Expect(raisedWindowWins, "raising a window to the last sibling should give it the cursor") &&
        Expect(
            modalTakesEverything,
            "while a window is modal no element outside it should take the cursor") &&
        Expect(modalReleases, "clearing the modal flag should restore the ordinary order");
}

static const TestSupport::Registration gUIWindowOcclusionTests{
    "UIEvent", "UI window occlusion tests should pass", RunUIWindowOcclusionTests };

static const TestSupport::Registration gUIWindowRaiseTests{
    "UIEvent", "UI window raise and modal tests should pass", RunUIWindowRaiseTests };
