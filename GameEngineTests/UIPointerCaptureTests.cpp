#include <cstddef>
#include <iostream>
#include <memory>

#include "../GameEngine/Platform/IInput.h"
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

#include "UIPointerCaptureTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEngine::Runtime::Button;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::Scene;

    constexpr float SurfaceWidth = 800.0f;
    constexpr float SurfaceHeight = 600.0f;
}

bool RunUIPointerCaptureTests()
{
    using namespace GameEngine::Runtime;
    std::cout << "running ui pointer capture tests\n";

    ObjectRegistry registry;
    Input input;
    RuntimeContext context{ registry, input };
    SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "Capture");
    GameObject* const canvas = scene->CreateGameObject("Canvas");
    static_cast<void>(canvas->AddComponent<Canvas>());

    // 끌리는 것을 흉내 낸다: 커서가 이 사각형 위에서 눌리고, 그 뒤 사각형이 커서 밖으로
    // 움직인다. 창을 끄는 일이 화면에서 정확히 이 모양이다.
    GameObject* const dragged = scene->CreateGameObject("Dragged");
    static_cast<void>(dragged->GetTransform().SetParent(&canvas->GetTransform()));
    RectTransform* const rect = dragged->AddComponent<RectTransform>();
    rect->SetAnchorMin({ 0.0f, 0.0f });
    rect->SetAnchorMax({ 0.0f, 0.0f });
    rect->SetOffsetMin({ 0.0f, 0.0f });
    rect->SetOffsetMax({ 100.0f, 40.0f });
    Button* const handle = dragged->AddComponent<Button>();

    if (!Expect(handle != nullptr, "the dragged element should have a button"))
    {
        return false;
    }
    static_cast<void>(sceneManager.AddScene(std::move(scene)));

    const UILayoutSystem layout;
    UIEventSystem events;
    const auto step = [&](const float x, const float y, const bool down, const bool press,
                          const bool release)
    {
        GameEngine::Platform::InputState state;
        state.cursor.x = static_cast<int>(x);
        state.cursor.y = static_cast<int>(y);
        const auto left = static_cast<std::size_t>(GameEngine::Platform::MouseButton::Left);
        state.mouseButtons[left] = down;
        state.mousePresses[left] = press ? 1 : 0;
        state.mouseReleases[left] = release ? 1 : 0;
        input.BeginFrameWithState(state);
        layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight);
        static_cast<void>(events.Synchronize(sceneManager, input));
    };

    bool passed = true;

    // 커서를 얹고 누른다. 이 프레임에는 눌린 모습과 쥠이 함께 참이다.
    step(20.0f, 20.0f, false, false, false);
    step(20.0f, 20.0f, true, true, false);
    passed = Expect(
        handle->IsPressed() && handle->HoldsPointer(),
        "pressing on the element should both press it and give it the pointer") && passed;

    // 이제 사각형이 커서 밖으로 움직인다 — 끌린 창이 하는 일이다. 커서는 그대로다.
    rect->SetOffsetMin({ 300.0f, 300.0f });
    rect->SetOffsetMax({ 400.0f, 340.0f });
    step(20.0f, 20.0f, true, false, false);

    passed = Expect(
        handle->HoldsPointer(),
        "an element that moved out from under the cursor should keep the pointer it holds") &&
        passed;
    // 눌린 <b>모습</b>은 함께 가지 않는다. 그 모습이 사람에게 「여기서 떼면 취소」를 말하므로,
    // 쥠이 이어진다고 눌림까지 이어지면 화면이 거짓말을 한다.
    passed = Expect(
        !handle->IsPressed(),
        "but it should not look pressed while the cursor is off it") && passed;

    // 여러 프레임이 지나도 쥠은 한 번 잡힌 그대로다 — 놓았다가 다시 잡는 일이 없어야 한 몸짓이
    // 한 몸짓으로 남는다.
    step(20.0f, 20.0f, true, false, false);
    step(20.0f, 20.0f, true, false, false);
    passed = Expect(
        handle->HoldsPointer(),
        "the hold should last across frames, not be dropped and retaken") && passed;

    // 그리고 그 사이에 <b>새로운 누름</b>이 시작되지 않았다. 끌기를 읽는 쪽은 이 모서리를
    // 세므로, 여기서 다시 서면 한 몸짓이 여럿으로 쪼개진다.
    passed = Expect(
        !handle->WasClickedThisFrame(),
        "no click should complete while the pointer is still held") && passed;

    // 밖에서 떼면 쥠이 풀리고, 클릭은 완성되지 않는다 — 밖에서 떼는 것은 취소다.
    step(20.0f, 20.0f, false, false, true);
    passed = Expect(
        !handle->HoldsPointer() && !handle->WasClickedThisFrame(),
        "releasing away from the element should end the hold without a click") && passed;

    // 받지 않게 되면 쥠도 함께 놓는다. 쥔 채로 남으면 그 끌기는 놓을 수 없는 것이 된다.
    step(20.0f, 320.0f, false, false, false);
    step(320.0f, 320.0f, true, true, false);
    passed = Expect(handle->HoldsPointer(), "the element should take the pointer again") && passed;
    handle->SetInteractable(false);
    passed = Expect(
        !handle->HoldsPointer(),
        "an element that stops taking input should let go of the pointer") && passed;

    return Expect(passed, "the pointer's hold should outlive the cursor leaving the element");
}

static const TestSupport::Registration gUIPointerCaptureTests{
    "UIEvent", "ui pointer capture tests should pass", RunUIPointerCaptureTests };
