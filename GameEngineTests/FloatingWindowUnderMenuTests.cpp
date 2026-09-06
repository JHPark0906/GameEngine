#include <cstddef>
#include <iostream>
#include <memory>

#include "Views/EditorFloatingPanelView.h"
#include "Views/EditorMenuBarView.h"
#include "Rules/EditorPanelCommon.h"

#include "Platform/IInput.h"
#include "Platform/PlatformServices.h"
#include "Rendering/CachedTextMeasure.h"
#include "Rendering/TextRasterizationCache.h"
#include "Runtime/Button.h"
#include "Runtime/Canvas.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RectTransform.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"
#include "Runtime/UIEventSystem.h"
#include "Runtime/UILayoutSystem.h"

#include "FloatingWindowUnderMenuTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    constexpr float SurfaceWidth = 1600.0f;
    constexpr float SurfaceHeight = 900.0f;

    /// <summary>떠 있는 창을 화면 가운데쯤에 둔다. 메뉴 막대와 겹치지 않는 자리다.</summary>
    constexpr float WindowX = 400.0f;
    constexpr float WindowY = 300.0f;
}

bool RunFloatingWindowUnderMenuTests()
{
    using namespace GameEngine::Runtime;
    std::cout << "running floating window under menu tests\n";

    auto rasterizer = GameEngine::Platform::PlatformServices::CreateTextRasterizer();
    if (!rasterizer || !rasterizer->Initialize())
    {
        std::cout << "  skipped: no text rasterizer on this machine\n";
        return true;
    }
    const auto measure = std::make_unique<GameEngine::Rendering::CachedTextMeasure>(
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(std::move(rasterizer)));

    ObjectRegistry registry;
    Input input;
    RuntimeContext context{ registry, input };
    SceneManager sceneManager{ context };
    auto scene = std::make_unique<Scene>(context, "EditorUI");
    Scene& sceneRef = *scene;

    // 편집기가 세우는 순서 그대로다: 떠 있는 층이 먼저, 메뉴가 뒤. 계층에서 뒤가 위이므로
    // 이 순서가 곧 「펼친 목록이 떠 있는 창을 덮는다」이며, 그것이 이 시험이 재는 조건이다.
    GameObject* const floatingLayer = sceneRef.CreateGameObject("EditorFloatingLayer");
    if (!floatingLayer || !floatingLayer->AddComponent<Canvas>())
    {
        return Expect(false, "the floating layer should assemble");
    }
    GameEditor::EditorFloatingPanelView console;
    console.Build(sceneRef, *floatingLayer, "Console");
    console.SetVisible(true);
    console.SetRect({ WindowX, WindowY, 420.0f, 260.0f });
    console.Synchronize(1.0f);

    GameObject* const menuCanvas = sceneRef.CreateGameObject("MenuBarCanvas");
    if (!menuCanvas || !menuCanvas->AddComponent<Canvas>())
    {
        return Expect(false, "the menu canvas should assemble");
    }
    GameEditor::EditorMenuBarView menuBar;
    menuBar.Build(sceneRef, *menuCanvas, GameEditor::RowFontSize);

    if (sceneManager.AddScene(std::move(scene)) == 0)
    {
        return Expect(false, "the scene should be added");
    }

    const UILayoutSystem layout;
    UIEventSystem events;
    const auto step = [&](const float x, const float y, const bool down, const bool press)
    {
        GameEngine::Platform::InputState state;
        state.cursor.x = static_cast<int>(x);
        state.cursor.y = static_cast<int>(y);
        const auto left = static_cast<std::size_t>(GameEngine::Platform::MouseButton::Left);
        state.mouseButtons[left] = down;
        state.mousePresses[left] = press ? 1 : 0;
        input.BeginFrameWithState(state);
        layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight, measure.get());
        static_cast<void>(events.Synchronize(sceneManager, input));
        console.Synchronize(1.0f);
        static_cast<void>(menuBar.Update({}, {}, SurfaceWidth, 1.0f));
    };

    // 제목줄 한가운데다. 메뉴 막대는 화면 맨 위에 있고 이 자리와 겹치지 않는다.
    const float titleX = WindowX + 100.0f;
    const float titleY = WindowY + GameEditor::HeaderHeight * 0.5f;

    bool passed = true;

    // ⑴ 닫힌 메뉴가 서 있어도 제목줄이 눌린다. 닫힌 목록은 꺼진 가지이므로 자리를 차지하지
    //    않아야 하며, 차지한다면 화면 아무 데도 누를 수 없게 된다.
    step(titleX, titleY, false, false);
    static_cast<void>(console.ConsumeTitleBarPressStart());
    step(titleX, titleY, true, true);
    passed = Expect(
        console.ConsumeTitleBarPressStart(),
        "with the menu bar standing, pressing the title bar should still start a drag") && passed;

    // ⑵ 그리고 잡은 채로 이어진다 — 창이 커서 밖으로 움직여도. 이것이 없으면 한 몸짓이 여러
    //    몸짓으로 쪼개진다.
    console.SetRect({ WindowX + 200.0f, WindowY + 150.0f, 420.0f, 260.0f });
    step(titleX, titleY, true, false);
    passed = Expect(
        !console.ConsumeTitleBarPressStart(),
        "and moving the window out from under the cursor should not restart it") && passed;

    // ⑶ 메뉴 막대 자체를 누르면 그것이 받는다. 막대가 화면 위쪽 띠 안에서는 이겨야 하고,
    //    그 밖에서는 아무것도 가로채지 않아야 한다 — 그 둘이 이 배치의 계약이다.
    step(titleX, titleY, false, false);
    step(titleX, titleY, false, false);
    static_cast<void>(console.ConsumeTitleBarPressStart());
    const GameEditor::MenuHeadingParts* firstHeading =
        menuBar.GetBar().headings.empty() ? nullptr : &menuBar.GetBar().headings.front();
    passed = Expect(firstHeading != nullptr, "the menu bar should have a heading") && passed;
    if (firstHeading && firstHeading->rect)
    {
        const RectTransform::Rect heading = firstHeading->rect->GetVisibleRect();
        step(heading.x + heading.width * 0.5f, heading.y + heading.height * 0.5f, true, true);
        passed = Expect(
            !console.ConsumeTitleBarPressStart(),
            "pressing the menu bar should not reach the window beneath it") && passed;
    }

    return Expect(
        passed, "a menu bar standing above a floating window should not take its title bar");
}

static const TestSupport::Registration gFloatingWindowUnderMenuTests{
    "EditorDocument", "floating window under menu tests should pass",
    RunFloatingWindowUnderMenuTests };
