#include "EditorToolbarScaleTests.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Views/EditorToolbarButton.h"
#include "Rules/EditorToolbarLayout.h"
#include "Views/EditorToolbarView.h"
#include "Platform/PlatformServices.h"
#include "Rendering/CachedTextMeasure.h"
#include "Rendering/TextRasterizationCache.h"
#include "Runtime/Canvas.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RectTransform.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "Runtime/UILayoutSystem.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>창의 논리 폭이다. 물리 1453의 200% 화면이 그 배율에서 이 값이다.</summary>
    constexpr float LogicalWindowWidth = 726.5f;
    constexpr float SurfaceHeight = 400.0f;

    /// <summary>
    /// 툴바가 실제로 선언하는 네 버튼이다.
    /// 독립된 기대 목록이어야 버튼을 지운 구현이 기대값도 함께 지워 검사를 통과하지 못한다.
    /// </summary>
    constexpr const char* Labels[] = { "Undo", "Redo", "Snap: Off", "Play" };

    /// <summary>주어진 배율에서 툴바를 세우고 관계 셋을 잰다.</summary>
    [[nodiscard]] bool CheckToolbarAtScale(const float Scale)
    {
    using namespace GameEngine::Runtime;
    const float SurfaceWidth = LogicalWindowWidth * Scale;

    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer || !rasterizer->Initialize())
    {
        std::cout << "  toolbar scale tests skipped: no text rasterizer on this machine\n";
        return true;
    }
    const auto measure = std::make_unique<GameEngine::Rendering::CachedTextMeasure>(
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(std::move(rasterizer)));

    ObjectRegistry registry;
    const Input input;
    RuntimeContext context{ registry, input };
    SceneManager sceneManager{ context };
    auto scene = std::make_unique<Scene>(context, "EditorUI");
    Scene& sceneRef = *scene;

    GameObject* const canvasObject = sceneRef.CreateGameObject("EditorUI");
    Canvas* const canvas = canvasObject ? canvasObject->AddComponent<Canvas>() : nullptr;
    if (!Expect(canvas != nullptr, "the scene has a canvas"))
    {
        return false;
    }
    canvas->SetScaleFactor(Scale);

    GameObject* const strip = sceneRef.CreateGameObject("Toolbar");
    RectTransform* const stripRect = strip ? strip->AddComponent<RectTransform>() : nullptr;
    if (!Expect(stripRect != nullptr, "and a toolbar strip"))
    {
        return false;
    }
    static_cast<void>(strip->GetTransform().SetParent(&canvasObject->GetTransform()));
    stripRect->SetAnchorMin({ 0.0f, 0.0f });
    stripRect->SetAnchorMax({ 1.0f, 0.0f });
    stripRect->SetOffsetMin({ 0.0f, 0.0f });
    stripRect->SetOffsetMax({ 0.0f, GameEditor::ToolbarLayoutMetrics{}.rowHeight });

    std::vector<GameEditor::ToolbarButtonParts> buttons;
    for (const char* const label : Labels)
    {
        buttons.push_back(GameEditor::BuildToolbarButton(
            sceneRef, *strip, label, GameEditor::ToolbarLabelFontSize));
    }
    static_cast<void>(sceneManager.AddScene(std::move(scene)));

    const UILayoutSystem layout;
    const GameEditor::ToolbarLayoutMetrics metrics;

    // 에디터와 같은 순서로 두 프레임을 돈다: 재고 → 그 폭으로 자리를 정하고 → 다시 재고 놓는다.
    // 한 프레임만 돌리면 자리를 정하기 전의 사각형을 재게 된다.
    std::vector<float> desired;
    for (int frame = 0; frame < 2; ++frame)
    {
        // 글꼴 크기는 논리 단위로 선언된 채 그대로 둔다. 배율을 곱하는 일은 그리는 가장자리가
        // 한 번만 하므로, 여기서 미리 곱하면 그 값이 두 번 곱해진다.
        layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight, measure.get());
        desired.clear();
        for (const GameEditor::ToolbarButtonParts& parts : buttons)
        {
            desired.push_back(parts.rect ? parts.rect->GetDesiredSize().width : 0.0f);
        }
        const GameEditor::ToolbarLayout placed = GameEditor::ComputeToolbarRowRects(
            desired, stripRect->GetResolvedRect().width, Scale, metrics);
        for (std::size_t index = 0; index < buttons.size(); ++index)
        {
            RectTransform* const rect = buttons[index].rect;
            if (!rect)
            {
                continue;
            }
            const GameEngine::UI::UIRect& box = placed.buttons[index];
            rect->SetOffsetMin({ box.x, box.y });
            rect->SetOffsetMax({ box.x + box.width, box.y + box.height });
        }
        stripRect->SetOffsetMax({ 0.0f, placed.height });
    }
    layout.Synchronize(sceneManager, SurfaceWidth, SurfaceHeight, measure.get());

    bool passed = true;

    // ⑴ 그려지는 사각형과 눌리는 사각형이 같다. 그리기는 GetResolvedRect를, 히트 테스트는
    // GetVisibleRect를 쓴다 — 둘이 갈라지면 버튼이 보이는 자리와 눌리는 자리가 어긋나고,
    // 사람에게는 "버튼이 안 눌린다"로 보인다.
    for (std::size_t index = 0; index < buttons.size(); ++index)
    {
        const RectTransform* const rect = buttons[index].rect;
        if (!rect)
        {
            continue;
        }
        const RectTransform::Rect& drawn = rect->GetResolvedRect();
        const RectTransform::Rect& clickable = rect->GetVisibleRect();
        const bool same = std::abs(drawn.x - clickable.x) < 0.01f &&
            std::abs(drawn.y - clickable.y) < 0.01f &&
            std::abs(drawn.width - clickable.width) < 0.01f &&
            std::abs(drawn.height - clickable.height) < 0.01f;
        if (!same)
        {
            std::cerr << "  \"" << Labels[index] << "\" is drawn at " << drawn.x << ","
                      << drawn.y << " " << drawn.width << "x" << drawn.height
                      << " but clickable at " << clickable.x << "," << clickable.y << " "
                      << clickable.width << "x" << clickable.height << "\n";
        }
        passed &= Expect(same, "every button is clickable exactly where it is drawn");
    }

    // ⑵ 그려지는 폭은 논리 폭 × 배율이다. 환산이 한 번 더 곱해지면 이 줄이 붉어진다.
    for (std::size_t index = 0; index < buttons.size(); ++index)
    {
        const RectTransform* const rect = buttons[index].rect;
        const RectTransform* const labelRect = buttons[index].labelRect;
        if (!rect || !labelRect)
        {
            continue;
        }
        // 글자 폭을 여기서 따로 잰다. 버튼이 얼마나 넓어야 하는지를 <b>버튼이 요구한 값</b>에서
        // 끌어오면, 그 값이 이미 두 배여도 기대값이 함께 두 배가 되어 시험이 통과한다 — 재는
        // 쪽과 재어지는 쪽이 같은 수가 되는 그 자리가 이 시험이 아무것도 지키지 못하는 자리다.
        GameEngine::Platform::TextRasterizationRequest request;
        request.text = Labels[index];
        request.fontSize = GameEditor::ToolbarLabelFontSize * Scale;
        const float logicalLabel =
            static_cast<float>(measure->Measure(request).width) / Scale;
        const float expected = (std::max)(
            metrics.minimumButtonWidth,
            logicalLabel + GameEditor::ToolbarLabelLeftInset * 2.0f) * Scale;
        static_cast<void>(labelRect);
        const float drawn = rect->GetResolvedRect().width;
        if (std::abs(drawn - expected) > 1.0f)
        {
            std::cerr << "  \"" << Labels[index] << "\" is drawn " << drawn
                      << "px wide but its label needs " << logicalLabel
                      << " logical, so " << expected << "px was expected\n";
        }
        passed &= Expect(
            std::abs(drawn - expected) <= 1.0f,
            "and is drawn its logical width times the scale, no more");
    }

    // ⑶ 모두 창 안에 있다. 넷은 웬만한 창에서 한 줄에 들므로 이것이 접힘을 재지는 않는다 —
    // 접힘 자체는 배치 함수에 폭 목록을 직접 넘기는 접힘 시험이 재고, 거기서는 버튼 수를
    // 마음대로 늘릴 수 있다. 여기서 지키는 것은 「자리를 정하는 계산이 창 밖으로 내보내지
    // 않는다」이며, 그것은 버튼이 넷이든 열둘이든 참이어야 한다.
    int outside = 0;
    for (std::size_t index = 0; index < buttons.size(); ++index)
    {
        const RectTransform* const rect = buttons[index].rect;
        if (!rect)
        {
            continue;
        }
        const RectTransform::Rect& drawn = rect->GetResolvedRect();
        if (drawn.x < -0.01f || drawn.x + drawn.width > SurfaceWidth + 0.01f)
        {
            std::cerr << "  \"" << Labels[index] << "\" runs from " << drawn.x << " to "
                      << drawn.x + drawn.width << " in a " << SurfaceWidth << "px window\n";
            ++outside;
        }
    }
    passed &= Expect(outside == 0, "and no button is laid out past the window's edge");
    return passed;
    }
}

bool RunEditorToolbarScaleTests()
{
    // 배율 1과 2에서 함께 돈다. 1에서는 논리와 물리가 같은 수라 곱하는 자리의 결함이
    // 보이지 않으므로, 한쪽만으로는 이 관계들을 지킬 수 없다.
    return TestSupport::ForEachUiScale(CheckToolbarAtScale);
}

static const TestSupport::Registration gEditorToolbarScaleTests{
    "EditorDocument", "editor toolbar scale tests should pass", RunEditorToolbarScaleTests };
