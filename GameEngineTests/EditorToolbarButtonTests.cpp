#include "EditorToolbarButtonTests.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Rules/EditorPanelCommon.h"
#include "Views/EditorToolbarButton.h"
#include "Views/EditorToolbarView.h"
#include "Platform/PlatformServices.h"
#include "Rendering/CachedTextMeasure.h"
#include "Rendering/TextRasterizationCache.h"
#include "Runtime/Canvas.h"
#include "Runtime/GameObject.h"
#include "Runtime/RectTransform.h"
#include "Runtime/Scene.h"
#include "Runtime/Transform.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/SceneManager.h"
#include "Runtime/UILayoutSystem.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>이 시험이 세우는 면의 크기다. 한 줄이 다 들어갈 만큼 넓게 둔다.</summary>
    constexpr float SurfaceWidth = 2000.0f;
    constexpr float SurfaceHeight = 200.0f;
}

bool RunEditorToolbarButtonTests()
{
    using namespace GameEngine::Runtime;

    // 글자를 재는 잣대가 없으면 이 시험이 묻는 것 자체가 성립하지 않는다. 없는 기계에서는
    // 통과시키지 않고 건너뛰었다고 말한다.
    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer || !rasterizer->Initialize())
    {
        std::cout << "  toolbar button tests skipped: no text rasterizer on this machine\n";
        return true;
    }
    const auto measure = std::make_unique<GameEngine::Rendering::CachedTextMeasure>(
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(std::move(rasterizer)));

    ObjectRegistry registry;
    const Input input;
    RuntimeContext context{ registry, input };
    SceneManager sceneManager{ context };
    auto scene = std::make_unique<Scene>(context, "Toolbar");
    Scene& sceneRef = *scene;
    // 캔버스가 있어야 배치가 이 계층을 UI 면으로 본다. 에디터도 툴바를 캔버스 아래에 둔다.
    GameObject* const canvasObject = sceneRef.CreateGameObject("EditorUI");
    if (!Expect(canvasObject != nullptr && canvasObject->AddComponent<Canvas>() != nullptr,
            "the test scene has a canvas"))
    {
        return false;
    }
    GameObject* const root = sceneRef.CreateGameObject("Toolbar");
    if (!Expect(root != nullptr, "the test scene has a toolbar root"))
    {
        return false;
    }
    static_cast<void>(root->GetTransform().SetParent(&canvasObject->GetTransform()));
    RectTransform* const rootRect = root->AddComponent<RectTransform>();
    if (!Expect(rootRect != nullptr, "and that root has a rectangle"))
    {
        return false;
    }
    rootRect->SetAnchorMin({ 0.0f, 0.0f });
    rootRect->SetAnchorMax({ 1.0f, 0.0f });
    rootRect->SetOffsetMin({ 0.0f, 0.0f });
    rootRect->SetOffsetMax({ 0.0f, GameEditor::ToolbarLayoutMetrics{}.rowHeight });

    // 에디터가 쓰는 그 함수로 세운다. 폭이 어디서 나오는지를 시험이 대신 정하지 않는다.
    struct Built
    {
        std::string label;
        GameEditor::ToolbarButtonParts parts;
    };
    std::vector<Built> buttons;
    for (const char* const label : { "New Project", "Undo", "Rename Scene", "Play" })
    {
        buttons.push_back(
            { label,
              GameEditor::BuildToolbarButton(
                  sceneRef, *root, label, GameEditor::ToolbarLabelFontSize) });
    }

    static_cast<void>(sceneManager.AddScene(std::move(scene)));
    const UILayoutSystem layout;
    layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight, measure.get());

    bool passed = true;
    for (const Built& built : buttons)
    {
        if (!Expect(built.parts.rect != nullptr, "every button has a rectangle"))
        {
            passed = false;
            continue;
        }
        const float width = built.parts.rect->GetDesiredSize().width;
        const float labelWidth =
            built.parts.labelRect ? built.parts.labelRect->GetDesiredSize().width : 0.0f;

        // 버튼이 요구하는 폭은 「글자 + 양쪽 여백」이며, 그보다 좁아지지는 않는다는 바닥이
        // 최소 폭이다. 짧은 글자는 바닥이 이기므로 「최소보다 넓다」를 요구하면 Play 같은
        // 버튼이 규칙을 어기지 않고도 붉어진다.
        const float expected = (std::max)(
            GameEditor::MinimumToolbarButtonWidth,
            labelWidth + GameEditor::ToolbarLabelLeftInset * 2.0f);
        if (std::abs(width - expected) > 0.01f)
        {
            std::cerr << "  \"" << built.label << "\" asked for " << width << "px but its label "
                      << "needs " << labelWidth << "px plus insets, so "
                      << expected << "px was expected\n";
        }
        passed &= Expect(
            std::abs(width - expected) <= 0.01f,
            "and asks for its label plus the inset on both sides, never less than the minimum");
    }

    // 긴 글자가 짧은 글자보다 넓다. 폭이 글자에서 나온다는 것을 이보다 직접 묻는 방법이 없다.
    if (buttons.size() >= 2 && buttons[0].parts.rect && buttons[1].parts.rect)
    {
        const float wide = buttons[0].parts.rect->GetDesiredSize().width;
        const float narrow = buttons[1].parts.rect->GetDesiredSize().width;
        if (wide <= narrow)
        {
            std::cerr << "  \"New Project\" asked for " << wide << "px and \"Undo\" for "
                      << narrow << "px\n";
        }
        passed &= Expect(wide > narrow, "a longer label makes a wider button");
    }
    return passed;
}

static const TestSupport::Registration gEditorToolbarButtonTests{
    "EditorDocument", "editor toolbar button tests should pass", RunEditorToolbarButtonTests };
