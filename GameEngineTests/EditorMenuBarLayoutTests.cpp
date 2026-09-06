#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Rules/EditorMenuBarLayout.h"
#include "Views/EditorMenuBarView.h"
#include "Rules/EditorMenuModel.h"
#include "Rules/EditorPanelCommon.h"

#include "Platform/PlatformServices.h"
#include "Rendering/CachedTextMeasure.h"
#include "Rendering/TextRasterizationCache.h"
#include "Runtime/Canvas.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RectTransform.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "Runtime/UILayoutSystem.h"

#include "EditorMenuBarLayoutTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEngine::UI::UIRect;

    constexpr float LogicalWindowWidth = 1200.0f;
    constexpr float SurfaceHeight = 800.0f;

    [[nodiscard]] bool Near(const float actual, const float expected)
    {
        return std::fabs(actual - expected) < 0.01f;
    }

    /// <summary>
    /// 한 배율에서 진짜 메뉴 막대를 세우고 재는다. 손으로 짠 트리가 아니라 <b>에디터가 쓰는 그
    /// 빌더</b>가 세운 것을 재는 이유는, 조립이 틀렸을 때 그것이 시험에 걸려야 하기 때문이다 —
    /// 산술을 흉내 낸 시험은 조립이 바뀌어도 통과한다.
    /// </summary>
    [[nodiscard]] bool CheckMenuBarAtScale(const float scale)
    {
        using namespace GameEngine::Runtime;
        const float surfaceWidth = LogicalWindowWidth * scale;

        auto rasterizer = TestSupport::CreateTestTextRasterizer();
        if (!rasterizer || !rasterizer->Initialize())
        {
            std::cout << "  menu bar layout tests skipped: no text rasterizer on this machine\n";
            return true;
        }
        const auto measure = std::make_unique<GameEngine::Rendering::CachedTextMeasure>(
            std::make_shared<GameEngine::Rendering::TextRasterizationCache>(
                std::move(rasterizer)));

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
        canvas->SetScaleFactor(scale);

        const GameEditor::MenuBarParts bar =
            GameEditor::BuildMenuBar(sceneRef, *canvasObject, GameEditor::RowFontSize);
        const GameEditor::MenuListParts list =
            GameEditor::BuildMenuList(sceneRef, *canvasObject, 0, GameEditor::RowFontSize);
        static_cast<void>(sceneManager.AddScene(std::move(scene)));

        const UILayoutSystem layout;
        // 에디터와 같이 두 프레임을 돈다. 한 프레임만 돌리면 자식이 재어지기 전의 부모를 읽는다.
        layout.Synchronize(sceneManager, surfaceWidth, SurfaceHeight, measure.get());
        layout.Synchronize(sceneManager, surfaceWidth, SurfaceHeight, measure.get());

        bool passed = true;
        std::cout << "running editor menu bar layout tests at scale " << scale << "\n";

        // ── 머리줄 ────────────────────────────────────────────────────────────
        const std::span<const GameEditor::EditorMenuDefinition> menus =
            GameEditor::GetEditorMenus();
        passed = Expect(bar.headings.size() == menus.size(), "every menu gets a heading") && passed;
        if (bar.headings.size() != menus.size())
        {
            return false;
        }

        // 머리줄의 폭은 자기 글자에서 올라온다. 글자가 긴 머리줄이 더 넓어야 하며, 그렇지
        // 않다면 폭이 글자와 무관하게 정해지고 있다는 뜻이다.
        bool widthFollowsTheLabel = true;
        for (const GameEditor::MenuHeadingParts& heading : bar.headings)
        {
            const float width = heading.rect ? heading.rect->GetDesiredSize().width : 0.0f;
            const float labelWidth =
                heading.label && heading.label->GetGameObject()
                ? heading.label->GetGameObject()->GetComponent<RectTransform>()
                      ->GetDesiredSize()
                      .width
                : 0.0f;
            if (labelWidth <= 0.0f || width <= labelWidth)
            {
                widthFollowsTheLabel = false;
            }
            // 여백은 배율을 탄다. 글자는 이미 배율이 곱해진 채로 재어져 오므로, 폭에서 글자를
            // 뺀 나머지가 정확히 여백 두 번이어야 한다 — 어긋나면 어딘가에서 한 번 더 곱해졌다.
            if (!Near(width - labelWidth, 2.0f * GameEditor::MenuHeadingPadding * scale))
            {
                widthFollowsTheLabel = false;
                std::cerr << "  heading width " << width << " minus label " << labelWidth
                          << " is not two paddings at scale " << scale << "\n";
            }
        }
        passed = Expect(
            widthFollowsTheLabel,
            "a heading's width should be its own label plus exactly two paddings") && passed;

        // 머리줄들은 붙어 있다. 그 붙임은 막대의 ContentFit이 하는 것이고, 그래서 머리줄의
        // 좌우 여백이 곧 머리줄 사이의 간격이 된다 — 같은 하나를 두 값이 정하지 않는다.
        bool headingsTouch = true;
        for (std::size_t index = 1; index < bar.headings.size(); ++index)
        {
            const RectTransform* const previous = bar.headings[index - 1].rect;
            const RectTransform* const current = bar.headings[index].rect;
            if (!previous || !current)
            {
                headingsTouch = false;
                continue;
            }
            const RectTransform::Rect& before = previous->GetResolvedRect();
            if (!Near(current->GetResolvedRect().x, before.x + before.width))
            {
                headingsTouch = false;
            }
        }
        passed = Expect(headingsTouch, "headings should sit flush against one another") && passed;

        // ── 펼친 목록 ─────────────────────────────────────────────────────────
        passed = Expect(
            list.rows.size() == menus[0].items.size(),
            "every row of the table, separators included, should exist") && passed;
        if (!list.rect || list.rows.size() != menus[0].items.size())
        {
            return false;
        }

        // 목록의 폭은 가장 넓은 줄에서 올라온다. 이것은 세로 LayoutGroup의 계약 그 자체이며,
        // 여기서 가장 긴 줄을 다시 고르지 않는다 — 다시 고르는 코드가 곧 두 번째 계산이다.
        float widestRow = 0.0f;
        for (const GameEditor::MenuRowParts& row : list.rows)
        {
            if (row.rect)
            {
                widestRow = (std::max)(widestRow, row.rect->GetDesiredSize().width);
            }
        }
        const float listWidth = list.rect->GetDesiredSize().width;
        std::cout << "  File menu: heading " << bar.headings[0].rect->GetDesiredSize().width
                  << " wide, list " << listWidth << " wide from its widest row " << widestRow
                  << "\n";
        passed = Expect(
            Near(listWidth, widestRow + 2.0f * GameEditor::MenuListInset * scale),
            "the list should be its widest row plus its own insets") && passed;

        // 단축키가 붙은 줄은 이름과 단축키 사이의 간격까지 함께 세어진다. 그래서 이름이 짧아도
        // 그 줄이 가장 넓을 수 있고, 단축키가 없는 줄은 그 간격을 지지 않는다.
        const GameEditor::MenuRowParts* withShortcut = nullptr;
        const GameEditor::MenuRowParts* withoutShortcut = nullptr;
        for (const GameEditor::MenuRowParts& row : list.rows)
        {
            if (row.isSeparator || !row.rect)
            {
                continue;
            }
            if (row.shortcut && !withShortcut)
            {
                withShortcut = &row;
            }
            if (!row.shortcut && !withoutShortcut)
            {
                withoutShortcut = &row;
            }
        }
        passed = Expect(
            withShortcut != nullptr && withoutShortcut != nullptr,
            "the File menu should hold both a row with a shortcut and one without") && passed;
        if (withShortcut && withoutShortcut)
        {
            const auto childWidth = [](const TextRenderer* const text)
            {
                if (!text || !text->GetGameObject())
                {
                    return 0.0f;
                }
                const RectTransform* const rect =
                    text->GetGameObject()->GetComponent<RectTransform>();
                return rect ? rect->GetDesiredSize().width : 0.0f;
            };
            const float rowWidth = withShortcut->rect->GetDesiredSize().width;
            const float parts = childWidth(withShortcut->label) + childWidth(withShortcut->shortcut);
            passed = Expect(
                Near(rowWidth - parts,
                    (GameEditor::MenuShortcutGap + 2.0f * GameEditor::MenuRowPadding) * scale),
                "a row with a shortcut should carry the gap between the two, once") && passed;

            const float plainWidth = withoutShortcut->rect->GetDesiredSize().width;
            passed = Expect(
                Near(plainWidth - childWidth(withoutShortcut->label),
                    2.0f * GameEditor::MenuRowPadding * scale),
                "a row without a shortcut should not pay for the gap") && passed;

            // 줄의 높이도 글자에서 올라온다. 높이를 상수로 두면 글자가 커질 때 줄만 그대로
            // 남아 글자가 넘치므로, 그 상수를 적을 자리가 없어야 한다.
            passed = Expect(
                withShortcut->rect->GetDesiredSize().height >
                    2.0f * GameEditor::MenuRowPadding * scale,
                "a row's height should come from its text, not from a constant") && passed;
        }

        // 구분선은 잴 글자가 없어도 자기 높이를 말한다. 0이면 표에 있는 선이 화면에서 사라진다.
        const GameEditor::MenuRowParts* separator = nullptr;
        for (const GameEditor::MenuRowParts& row : list.rows)
        {
            if (row.isSeparator && row.rect)
            {
                separator = &row;
                break;
            }
        }
        passed = Expect(separator != nullptr, "the File menu should hold a separator") && passed;
        if (separator)
        {
            passed = Expect(
                Near(separator->rect->GetDesiredSize().height,
                    GameEditor::MenuSeparatorHeight * scale),
                "a separator should declare its own height and take the scale") && passed;
        }

        // ── 자리 ──────────────────────────────────────────────────────────────
        const RectTransform::Rect& firstHeading = bar.headings[0].rect->GetResolvedRect();
        const UIRect heading{ firstHeading.x, firstHeading.y, firstHeading.width,
                              firstHeading.height };
        const float listHeight = list.rect->GetDesiredSize().height;

        const GameEditor::MenuListPlacement placed =
            GameEditor::PlaceMenuList(heading, listWidth, listHeight, surfaceWidth, scale);
        passed = Expect(
            Near(placed.panel.x, heading.x) &&
                Near(placed.panel.y, heading.y + heading.height),
            "the list should open flush under its heading's left edge") && passed;
        passed = Expect(
            Near(placed.panel.width, listWidth) && Near(placed.panel.height, listHeight),
            "a list that clears both lower bounds should keep the size the layout gave it") &&
            passed;
        passed = Expect(placed.shortcutsFit, "a list that fits should keep its shortcuts") && passed;

        // ── 줄 안에서 두 글자가 어디 서는가 ──────────────────────────────────
        // 여기부터는 <b>목록을 실제로 놓은 뒤</b>에 잰다. 놓기 전의 목록은 폭이 0이고, 그때
        // 줄도 폭이 0이라 자식이 어디 있는지 물어도 답이 없다 — 화면과 같은 순서를 밟아야
        // 화면과 같은 답이 나온다.
        list.rect->SetOffsetMin({ placed.panel.x, placed.panel.y });
        list.rect->SetOffsetMax(
            { placed.panel.x + placed.panel.width, placed.panel.y + placed.panel.height });
        layout.Synchronize(sceneManager, surfaceWidth, SurfaceHeight, measure.get());

        // 이름과 단축키의 배치 사각형과 정렬 속성을 확인한다.
        // 두 사각형은 행을 가로지르며 각자 반대쪽 끝에 정렬되므로 사각형 자체가 겹치는 것은 허용한다.
        if (withShortcut && withShortcut->label && withShortcut->shortcut)
        {
            const auto rectOf = [](const TextRenderer* const text)
            {
                return text->GetGameObject()->GetComponent<RectTransform>()->GetVisibleRect();
            };
            const RectTransform::Rect rowRect = withShortcut->rect->GetVisibleRect();
            const RectTransform::Rect labelRect = rectOf(withShortcut->label);
            const RectTransform::Rect shortcutRect = rectOf(withShortcut->shortcut);
            const float inset = GameEditor::MenuRowPadding * scale;

            passed = Expect(rowRect.width > 0.0f, "a placed row should have a width") && passed;
            passed = Expect(
                Near(labelRect.x, rowRect.x + inset) &&
                    Near(labelRect.x + labelRect.width, rowRect.x + rowRect.width - inset),
                "the name should span the row, inset at both ends") && passed;
            passed = Expect(
                Near(shortcutRect.x, rowRect.x + inset) &&
                    Near(shortcutRect.x + shortcutRect.width,
                        rowRect.x + rowRect.width - inset),
                "and so should the shortcut, so neither is pinned to the row's corner") && passed;
            passed = Expect(
                withShortcut->label->GetAlignment() == TextRenderer::Alignment::Left &&
                    withShortcut->shortcut->GetAlignment() == TextRenderer::Alignment::Right,
                "with the two pulled to opposite ends, which is what keeps them apart") && passed;
        }

        // 목록은 머리줄보다 좁아지지 않는다. 좁으면 눌린 머리줄의 오른쪽이 목록 밖으로 나온다.
        const UIRect wideHeading{ 0.0f, 0.0f, listWidth + 200.0f, heading.height };
        passed = Expect(
            Near(GameEditor::PlaceMenuList(wideHeading, listWidth, listHeight, surfaceWidth, scale)
                     .panel.width,
                listWidth + 200.0f),
            "a list should never be narrower than the heading it hangs from") && passed;

        // 아무것도 재지 못한 프레임에서도 알아볼 만한 폭이 남는다. 그 하한은 논리 픽셀이므로
        // 배율을 탄다 — 안 타면 200% 화면에서 이 목록만 절반 크기로 열린다.
        const UIRect narrowHeading{ 0.0f, 0.0f, 1.0f, heading.height };
        passed = Expect(
            Near(GameEditor::PlaceMenuList(narrowHeading, 0.0f, listHeight, surfaceWidth, scale)
                     .panel.width,
                GameEditor::MinimumMenuListWidth * scale),
            "an unmeasured list should fall back to a visible width that takes the scale") &&
            passed;

        // 오른쪽 끝의 메뉴는 들어갈 만큼만 왼쪽으로 민다. 오른쪽 정렬로 뒤집지 않는다.
        const UIRect rightmost{ surfaceWidth - 40.0f, 0.0f, 40.0f, heading.height };
        const GameEditor::MenuListPlacement pushed =
            GameEditor::PlaceMenuList(rightmost, listWidth, listHeight, surfaceWidth, scale);
        passed = Expect(
            Near(pushed.panel.x + pushed.panel.width, surfaceWidth) &&
                pushed.panel.x < rightmost.x,
            "a list at the right edge should be pushed just inside the window") && passed;

        // 창이 목록보다 좁으면 더 밀 곳이 없다. 왼쪽에 붙여 이름을 남기고, 잘려 나가는 오른쪽의
        // 단축키는 아예 그리지 않는다 — 반쯤 잘린 단축키는 잘못된 키로 읽힌다.
        const GameEditor::MenuListPlacement clipped =
            GameEditor::PlaceMenuList(rightmost, listWidth, listHeight, listWidth * 0.5f, scale);
        passed = Expect(
            Near(clipped.panel.x, 0.0f) && !clipped.shortcutsFit,
            "a window narrower than the list should keep the names and drop the shortcuts") &&
            passed;

        return passed;
    }
}

bool RunEditorMenuBarLayoutTests()
{
    return TestSupport::ForEachUiScale(CheckMenuBarAtScale);
}

static const TestSupport::Registration gEditorMenuBarLayoutTests{
    "EditorDocument", "editor menu bar layout tests should pass", RunEditorMenuBarLayoutTests };
