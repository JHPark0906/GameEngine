#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "Views/EditorMenuBarView.h"
#include "Rules/EditorMenuModel.h"
#include "Rules/EditorPanelCommon.h"

#include "Platform/PlatformServices.h"
#include "Math/Matrix.h"
#include "Platform/IAudioOutput.h"
#include "Platform/IInput.h"
#include "Platform/ITextMeasure.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/CachedTextMeasure.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Rendering/TextRasterizationCache.h"
#include "Runtime/Canvas.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/RectTransform.h"
#include "Runtime/Scene.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "Runtime/UIEventSystem.h"
#include "Runtime/UILayoutSystem.h"
#include "SceneRendering/SceneRenderPass.h"

#include "MenuShortcutPlacementTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>글리프들이 실제로 덮는 x 범위다. 없으면 값이 없다 — 그리지 않았다는 뜻이다.</summary>
    struct Span
    {
        float left = 0.0f;
        float right = 0.0f;
        bool drawn = false;
    };

    /// <summary>
    /// 지정한 y 범위에서 그려지는 모든 글리프의 x 범위를 반환한다.
    /// 같은 행의 이름과 단축키를 모두 포함하도록 draw 하나만 고르지 않는다.
    /// </summary>
    [[nodiscard]] Span SpanOfTextsIn(
        const std::vector<const GameEngine::Rendering::TextDraw*>& texts, const float top,
        const float bottom)
    {
        Span span;
        span.left = (std::numeric_limits<float>::max)();
        span.right = -(std::numeric_limits<float>::max)();
        for (const GameEngine::Rendering::TextDraw* const draw : texts)
        {
            if (!draw || !draw->glyphs || draw->glyphs->empty())
            {
                continue;
            }
            // 화면 공간 글자의 자리는 localToWorld의 평행이동에 실린다. 글리프의 중심은
            // 그 자리 기준의 오프셋이다.
            const GameEngine::Math::Vector3 origin = draw->localToWorld.GetTranslation();
            const float originY = origin.GetY();
            if (originY < top || originY > bottom)
            {
                continue;
            }
            const float originX = origin.GetX();
            span.drawn = true;
            for (const GameEngine::Rendering::TextGlyphQuad& glyph : *draw->glyphs)
            {
                span.left = (std::min)(span.left, originX + glyph.centerX - glyph.width * 0.5f);
                span.right = (std::max)(span.right, originX + glyph.centerX + glyph.width * 0.5f);
            }
        }
        return span;
    }
}

static bool CheckMenuShortcutPlacement(const float scale)
{
    using namespace GameEngine::Runtime;
    std::cout << "running menu shortcut placement tests\n";

    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!Expect(rasterizer != nullptr, "the bundled test font is loaded"))
    {
        return false;
    }
    const std::shared_ptr<GameEngine::Rendering::TextRasterizationCache> textCache =
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(
            std::move(rasterizer));
    GameEngine::Rendering::CachedTextMeasure measure{ textCache };

    Game game{ nullptr, nullptr };
    auto scene = std::make_unique<Scene>(game.GetRuntimeContext(), "MenuShortcuts");
    GameObject* const canvasObject = scene->CreateGameObject("MenuBarCanvas");
    Canvas* const canvas = canvasObject ? canvasObject->AddComponent<Canvas>() : nullptr;
    if (!canvas)
    {
        return Expect(false, "the menu canvas should assemble");
    }
    canvas->SetScaleFactor(scale);
    Scene& sceneRef = *scene;

    GameEditor::EditorMenuBarView menuBar;
    menuBar.Build(sceneRef, *canvasObject, GameEditor::RowFontSize);
    if (game.AddScene(std::move(scene)) == 0)
    {
        return Expect(false, "the scene should be added");
    }

    const UILayoutSystem layout;
    UIEventSystem events;
    const float surfaceWidth = 1600.0f * scale;
    const float surfaceHeight = 900.0f * scale;

    // 메뉴는 <b>버튼의 클릭</b>으로 열린다. 그 판정은 이벤트 시스템의 것이므로, 여기서도
    // 사람이 하는 것과 같은 길로 연다 — 뷰에게 「열려라」라고 말하는 지름길은 없다.
    const auto frameStep = [&](const float x, const float y, const bool down, const bool press,
                                const bool release)
    {
        GameEngine::Platform::InputState state;
        state.cursor.x = static_cast<int>(x);
        state.cursor.y = static_cast<int>(y);
        const auto left = static_cast<std::size_t>(GameEngine::Platform::MouseButton::Left);
        state.mouseButtons[left] = down;
        state.mousePresses[left] = press ? 1 : 0;
        state.mouseReleases[left] = release ? 1 : 0;
        game.GetInput().BeginFrameWithState(state);
        layout.Synchronize(game.GetSceneManager(), surfaceWidth, surfaceHeight, &measure);
        static_cast<void>(events.Synchronize(game.GetSceneManager(), game.GetInput()));
        static_cast<void>(menuBar.Update({ release, false }, {}, surfaceWidth, scale));
        layout.Synchronize(game.GetSceneManager(), surfaceWidth, surfaceHeight, &measure);
    };
    frameStep(-1.0f, -1.0f, false, false, false);
    frameStep(-1.0f, -1.0f, false, false, false);

    const GameEditor::MenuBarParts& bar = menuBar.GetBar();
    if (!Expect(!bar.headings.empty() && bar.headings.front().rect, "the bar should have File"))
    {
        return false;
    }
    const RectTransform::Rect fileHeading = bar.headings.front().rect->GetVisibleRect();
    const float headingX = fileHeading.x + fileHeading.width * 0.5f;
    const float headingY = fileHeading.y + fileHeading.height * 0.5f;

    // File 메뉴를 연다. 목록이 놓여야 줄이 폭을 갖고, 폭이 있어야 정렬이 뜻을 갖는다.
    frameStep(headingX, headingY, true, true, false);
    frameStep(headingX, headingY, false, false, true);
    frameStep(headingX, headingY, false, false, false);

    const GameEditor::MenuListParts* list = menuBar.GetOpenList();
    if (!list)
    {
        // 여는 것은 버튼의 클릭이 하므로 포인터 없이 열리지 않는 빌드가 있을 수 있다. 그때는
        // 메뉴 자체를 세우는 다른 시험이 답하므로 여기서는 조용히 넘어가지 않고 말한다.
        return Expect(false, "the File menu should be open for this test to mean anything");
    }

    const GameEditor::MenuRowParts* withShortcut = nullptr;
    for (const GameEditor::MenuRowParts& row : list->rows)
    {
        if (!row.isSeparator && row.shortcut && row.rect)
        {
            withShortcut = &row;
            break;
        }
    }
    if (!Expect(withShortcut != nullptr, "the File menu should hold a row with a shortcut"))
    {
        return false;
    }

    GameEngine::SceneRendering::SceneRenderPass pass{ textCache };
    GameEngine::Rendering::RenderFrameBuilder builder;
    builder.SetRenderTargetSize({
        static_cast<unsigned int>(surfaceWidth), static_cast<unsigned int>(surfaceHeight) });
    pass.Collect(game, builder);
    const GameEngine::Rendering::RenderFrame frame = std::move(builder).Build();
    const std::vector<const GameEngine::Rendering::TextDraw*> texts =
        frame.GetDraws<GameEngine::Rendering::TextDraw>(GameEngine::Rendering::RenderPass::Overlay);

    if (texts.empty())
    {
        return Expect(false, "the menu must produce text draws with its registered font");
    }

    const RectTransform::Rect row = withShortcut->rect->GetResolvedRect();
    const Span drawn = SpanOfTextsIn(texts, row.y, row.y + row.height);

    bool passed = Expect(
        drawn.drawn, "the row with a shortcut should put glyphs on screen at all");
    if (drawn.drawn)
    {
        // 이 줄에는 이름과 단축키가 함께 있다. 둘을 합친 글리프 범위는 줄의 <b>오른쪽 끝까지</b>
        // 닿아야 한다. 왼쪽에만 몰려 있으면 단축키가 그려지지 않았거나 이름 위에 포개진 것이고,
        // 화면에서는 그 둘이 같은 모습이다 — 「글자가 없다」.
        const float rightEdge = row.x + row.width;
        std::cout << "  row " << row.x << ".." << rightEdge << ", glyphs " << drawn.left
                  << ".." << drawn.right << "\n";
        // 오른쪽 정렬은 행의 폭을 사용하며 단축키의 실제 글리프가 행 안에 있어야 한다.
        const float slack = GameEditor::MenuRowPadding * 2.0f * scale;
        passed = Expect(
            drawn.right <= rightEdge + slack && drawn.left >= row.x - slack,
            "every glyph in the row should be inside the row") && passed;
        // 그리고 오른쪽 끝까지 닿아야 한다. 안에만 있고 왼쪽에 몰려 있으면 단축키가 이름 위에
        // 포개졌거나 그려지지 않은 것이다.
        passed = Expect(
            drawn.right >= rightEdge - slack,
            "and the shortcut should stand at the row's right edge") && passed;
        passed = Expect(
            drawn.left <= row.x + slack, "with the name still at the left edge") && passed;
    }
    return passed;
}

bool RunMenuShortcutPlacementTests()
{
    return TestSupport::ForEachUiScale(CheckMenuShortcutPlacement);
}

static const TestSupport::Registration gMenuShortcutPlacementTests{
    "EditorDocument", "menu shortcut placement tests should pass",
    RunMenuShortcutPlacementTests };
