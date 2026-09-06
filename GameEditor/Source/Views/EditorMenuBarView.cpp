#include "Views/EditorMenuBarView.h"

#include <string>
#include <utility>

#include "Rules/EditorMenuBarLayout.h"
#include "Rules/EditorMenuModel.h"
#include "Rules/EditorPanelCommon.h"

#include "Assets/AssetReference.h"
#include "Runtime/Button.h"
#include "Runtime/ContentFit.h"
#include "Runtime/GameObject.h"
#include "Runtime/LayoutElement.h"
#include "Runtime/LayoutGroup.h"
#include "Runtime/RectTransform.h"
#include "Runtime/Scene.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "Runtime/UIWindow.h"

namespace GameEditor
{

namespace
{
    using GameEngine::Math::Vector2;
    using GameEngine::Runtime::Button;
    using GameEngine::Runtime::ContentFit;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::LayoutElement;
    using GameEngine::Runtime::LayoutGroup;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::SpriteRenderer;
    using GameEngine::Runtime::TextRenderer;

    /// <summary>목록과 머리줄의 바탕이다. 9-슬라이스라 어느 크기로 늘려도 모서리가 유지된다.</summary>
    constexpr const char* PanelSprite = "Sprites/panel-32.png";

    /// <summary>이 오브젝트 아래에 사각형 하나를 만든다. 배경도 글자도 없는 자리다.</summary>
    [[nodiscard]] GameObject* AddRect(
        Scene& scene, GameObject& parent, const std::string& name)
    {
        GameObject* const object = scene.CreateGameObject(name);
        if (!object)
        {
            return nullptr;
        }
        static_cast<void>(object->GetTransform().SetParent(&parent.GetTransform()));
        RectTransform* const rect = object->AddComponent<RectTransform>();
        if (!rect)
        {
            return nullptr;
        }
        rect->SetAnchorMin({ 0.0f, 0.0f });
        rect->SetAnchorMax({ 0.0f, 0.0f });
        rect->SetOffsetMin({ 0.0f, 0.0f });
        rect->SetOffsetMax({ 0.0f, 0.0f });
        return object;
    }

    /// <summary>
    /// 글자 하나를 세운다. <c>LayoutElement</c>가 함께 붙으므로 이 오브젝트는 자기 글자를 재고,
    /// 그 답이 부모에게 올라간다 — 폭이 만들어지는 곳은 오직 여기다.
    /// </summary>
    [[nodiscard]] TextRenderer* AddLabel(
        Scene& scene, GameObject& parent, const std::string& name, const std::string& text,
        const GameEngine::Math::Color& color, const float fontSize,
        const TextRenderer::Alignment alignment = TextRenderer::Alignment::Left)
    {
        GameObject* const object = AddRect(scene, parent, name);
        if (!object)
        {
            return nullptr;
        }
        TextRenderer* const label = object->AddComponent<TextRenderer>();
        if (!label)
        {
            return nullptr;
        }
        label->SetText(text);
        label->SetColor(color);
        label->SetSpace(TextRenderer::Space::Screen);
        label->SetAlignment(alignment);
        // 세로 가운데는 TextRenderer가 잡는다. 배치된 글자 블록의 높이를 알아야 하는 값이라
        // 여기서 어림할 수 없다.
        label->SetVerticalAlignment(TextRenderer::VerticalAlignment::Middle);
        label->SetFontSize(fontSize);

        if (LayoutElement* const element = object->AddComponent<LayoutElement>())
        {
            element->SetFit(LayoutElement::Fit::Both);
        }
        return label;
    }

    /// <summary>
    /// 강조되지 않은 줄과 머리줄의 색이다. 자기 바탕과 같은 색이므로 보이지 않는다 — 강조를
    /// 껐다 켜는 일이 색 하나를 바꾸는 것이 되어, 컴포넌트를 붙였다 뗐다 하지 않아도 된다.
    /// </summary>
    constexpr GameEngine::Math::Color QuietRow{ PanelColor };
    constexpr GameEngine::Math::Color QuietHeading{ HeaderColor };

    /// <summary>목록과 머리줄에 바탕을 깐다.</summary>
    SpriteRenderer* AddBackground(GameObject& object, const GameEngine::Math::Color& color)
    {
        SpriteRenderer* const background = object.AddComponent<SpriteRenderer>();
        if (!background)
        {
            return nullptr;
        }
        background->SetSprite(GameEngine::Assets::AssetReference::Parse(PanelSprite));
        background->SetDrawMode(SpriteRenderer::DrawMode::Sliced);
        // 사각형은 RectTransform이 정했지만, 그 사각형을 쓰겠다고 말하는 것은 이 한 줄이다.
        background->SetSpace(SpriteRenderer::Space::Screen);
        background->SetColor(color);
        return background;
    }
}

MenuBarParts BuildMenuBar(Scene& scene, GameObject& parent, const float fontSize)
{
    MenuBarParts bar;
    bar.object = AddRect(scene, parent, "MenuBar");
    if (!bar.object)
    {
        return bar;
    }
    bar.rect = bar.object->GetComponent<RectTransform>();
    // 막대는 창 위쪽을 가로지른다. 세로 크기는 머리줄이 요구하는 만큼이며, 셸이 이 사각형의
    // 위쪽 y만 정한다.
    if (bar.rect)
    {
        bar.rect->SetAnchorMin({ 0.0f, 0.0f });
        bar.rect->SetAnchorMax({ 1.0f, 0.0f });
    }
    static_cast<void>(AddBackground(*bar.object, HeaderColor));

    // 머리줄들을 왼쪽부터 붙여 놓는 것은 이 한 줄이다. 간격도 여백도 없으므로 머리줄의 좌우
    // 여백이 곧 머리줄 사이의 간격이 된다 — 같은 하나를 두 값이 정하지 않는다.
    if (ContentFit* const fit = bar.object->AddComponent<ContentFit>())
    {
        fit->SetDirection(ContentFit::Direction::Horizontal);
        fit->SetSpacing(0.0f);
        fit->SetPadding(0.0f);
    }
    // 막대의 <b>높이</b>는 가장 높은 머리줄이 정한다. 높이 상수를 두면 글자를 키운 날 막대만
    // 그대로 남아 글자가 띠 밖으로 넘치고, 그것은 화면에서 "글꼴이 크다"로만 보인다.
    if (LayoutGroup* const group = bar.object->AddComponent<LayoutGroup>())
    {
        group->SetDirection(LayoutGroup::Direction::Horizontal);
        group->SetPadding(0.0f);
    }
    if (LayoutElement* const element = bar.object->AddComponent<LayoutElement>())
    {
        // 세로로만 자란다. 가로는 창을 가로지르는 앵커가 정하고, 그 위를 ContentFit이 덮는다.
        element->SetFit(LayoutElement::Fit::Vertical);
    }

    for (const EditorMenuDefinition& menu : GetEditorMenus())
    {
        const std::string title(menu.title);
        MenuHeadingParts heading;
        heading.object = AddRect(scene, *bar.object, title);
        if (!heading.object)
        {
            continue;
        }
        heading.rect = heading.object->GetComponent<RectTransform>();
        heading.background = AddBackground(*heading.object, QuietHeading);

        // 커서가 이 머리줄 위에 있는지는 이벤트 시스템이 답한다. 색도 그 답을 따라 이 버튼이
        // 칠하므로, 강조를 위해 좌표를 다시 재는 자리가 없다.
        heading.button = heading.object->AddComponent<Button>();
        if (heading.button)
        {
            heading.button->SetNormalColor(QuietHeading);
            heading.button->SetHoveredColor(PanelColor);
            heading.button->SetPressedColor(PanelColor);
        }

        // 머리줄 자신에게는 글자가 없다. 글자는 자식에 살고, 이 오브젝트는 그 자식이 요구한
        // 크기에 좌우 여백을 더해 받는 그릇이다 — 툴바 버튼과 같은 모양이다.
        if (LayoutGroup* const group = heading.object->AddComponent<LayoutGroup>())
        {
            group->SetDirection(LayoutGroup::Direction::Horizontal);
            group->SetPadding(MenuHeadingPadding);
        }
        // 머리줄의 폭은 막대의 ContentFit이 요구 폭 그대로 주지만 높이는 주지 않는다 — 쌓이는
        // 축만 정하는 것이 그 컴포넌트의 계약이다. 그래서 세로로 자란다고 여기서 말한다.
        if (LayoutElement* const element = heading.object->AddComponent<LayoutElement>())
        {
            element->SetFit(LayoutElement::Fit::Vertical);
        }
        heading.label =
            AddLabel(scene, *heading.object, title + " Label", title, TextColor, fontSize);
        bar.headings.push_back(heading);
    }
    return bar;
}

MenuListParts BuildMenuList(
    Scene& scene, GameObject& parent, const std::size_t menuIndex, const float fontSize)
{
    MenuListParts list;
    const std::span<const EditorMenuDefinition> menus = GetEditorMenus();
    if (menuIndex >= menus.size())
    {
        return list;
    }
    const EditorMenuDefinition& menu = menus[menuIndex];

    const std::string name(menu.title);
    list.object = AddRect(scene, parent, name + " Menu");
    if (!list.object)
    {
        return list;
    }
    list.rect = list.object->GetComponent<RectTransform>();
    static_cast<void>(AddBackground(*list.object, PanelColor));

    // 펼친 목록은 창이다 — 자기가 덮은 것을 이겨야 하고, 그 「이긴다」를 답하는 것이 창의
    // 쌓인 순서다. 모달은 아니다: 모달은 밖을 눌러도 아무 일이 없는 것이고 메뉴는 밖을
    // 누르면 닫히는 것이라, 그 둘은 서로 다른 몸짓이다.
    static_cast<void>(list.object->AddComponent<GameEngine::Runtime::UIWindow>());

    // 판을 덮는 버튼이다. 색은 네 상태가 모두 판과 같아 보이지 않으며, 하는 일은 막는 것뿐이다.
    list.blocker = list.object->AddComponent<Button>();
    if (list.blocker)
    {
        list.blocker->SetNormalColor(PanelColor);
        list.blocker->SetHoveredColor(PanelColor);
        list.blocker->SetPressedColor(PanelColor);
        list.blocker->SetDisabledColor(PanelColor);
    }

    // 두 컴포넌트가 나란히 붙는다. 세로 LayoutGroup은 가로지르는 방향 — 곧 목록의 폭 — 을
    // 가장 넓은 줄에서 얻고, 세로 ContentFit은 줄들을 위에서부터 쌓으며 각 줄이 요구한 높이를
    // 그대로 준다. 「가장 긴 줄이 폭을 정한다」는 규칙을 여기서 다시 쓰지 않는 이유가 이것이다:
    // 그것은 이미 LayoutGroup의 계약이다.
    if (LayoutGroup* const group = list.object->AddComponent<LayoutGroup>())
    {
        group->SetDirection(LayoutGroup::Direction::Vertical);
        group->SetPadding(MenuListInset);
    }
    if (ContentFit* const fit = list.object->AddComponent<ContentFit>())
    {
        fit->SetDirection(ContentFit::Direction::Vertical);
        fit->SetSpacing(0.0f);
        fit->SetPadding(MenuListInset);
    }

    int rowIndex = 0;
    for (const EditorMenuItem& item : menu.items)
    {
        MenuRowParts row;
        row.isSeparator = item.IsSeparator();
        row.command = item.command;
        const std::string rowName = name + " Row " + std::to_string(rowIndex++);
        row.object = AddRect(scene, *list.object, rowName);
        if (!row.object)
        {
            continue;
        }
        row.rect = row.object->GetComponent<RectTransform>();
        if (row.rect)
        {
            // 목록의 세로 ContentFit은 줄의 y와 높이만 정한다. 가로로는 줄이 목록을 가로질러야
            // 하며 — 강조가 목록 폭 전체를 덮어야 어느 줄이 겨눠졌는지 읽힌다 — 좌우로는
            // 목록의 안쪽 여백만큼 들어온다.
            row.rect->SetAnchorMin({ 0.0f, 0.0f });
            row.rect->SetAnchorMax({ 1.0f, 0.0f });
            row.rect->SetOffsetMin({ MenuListInset, 0.0f });
            row.rect->SetOffsetMax({ -MenuListInset, 0.0f });
        }

        if (row.isSeparator)
        {
            // 선에는 잴 글자가 없으므로 자기 높이를 스스로 말한다. 최소 크기는 잴 것이 없을
            // 때에도 남는 값이라, 글꼴을 열지 못한 화면에서도 선이 자리를 잃지 않는다.
            if (LayoutElement* const element = row.object->AddComponent<LayoutElement>())
            {
                element->SetFit(LayoutElement::Fit::None);
                element->SetMinimumSize({ 0.0f, MenuSeparatorHeight });
            }
            // 선 자체는 그 줄 한가운데를 가로지르는 자식이다. 줄 전체를 칠하면 두꺼운 띠가
            // 되므로, 자리를 차지하는 것(줄)과 보이는 것(선)을 나눈다.
            if (GameObject* const line = AddRect(scene, *row.object, rowName + " Line"))
            {
                if (RectTransform* const lineRect = line->GetComponent<RectTransform>())
                {
                    lineRect->SetAnchorMin({ 0.0f, 0.5f });
                    lineRect->SetAnchorMax({ 1.0f, 0.5f });
                    lineRect->SetOffsetMin({ MenuRowPadding, -0.5f });
                    lineRect->SetOffsetMax({ -MenuRowPadding, 0.5f });
                }
                static_cast<void>(AddBackground(*line, DimTextColor));
            }
            list.rows.push_back(row);
            continue;
        }

        // 강조 바탕은 처음부터 붙어 있고 색만 바뀐다. 커서가 얹힐 때 컴포넌트를 붙였다 떼면
        // 그 프레임의 배치가 한 번 흔들리고, 흔들리는 쪽은 사람이 지금 겨누고 있는 줄이다.
        row.background = AddBackground(*row.object, QuietRow);
        row.button = row.object->AddComponent<Button>();
        if (row.button)
        {
            // 평상시에는 판과 같은 색이라 보이지 않고, 커서가 얹히면 한 단계 밝아진다.
            // 흐린 줄의 색도 판과 같다 — 흐림은 바탕이 아니라 글자가 말한다.
            row.button->SetNormalColor(QuietRow);
            row.button->SetHoveredColor(HeaderColor);
            row.button->SetPressedColor(HeaderColor);
            row.button->SetDisabledColor(QuietRow);
        }

        // 줄의 폭도 높이도 글자에서 올라온다. 여백은 LayoutGroup의 것이라 두 방향에 함께
        // 붙으므로, 줄 높이를 위한 상수를 따로 둘 자리가 없다 — 글자가 커지면 줄도 자란다.
        if (LayoutGroup* const group = row.object->AddComponent<LayoutGroup>())
        {
            group->SetDirection(LayoutGroup::Direction::Horizontal);
            group->SetPadding(MenuRowPadding);
            group->SetSpacing(MenuShortcutGap);
        }
        // 두 글자는 줄을 <b>가로질러</b> 놓이고, 각자 자기 끝으로 정렬한다. 이름은 왼쪽,
        // 단축키는 오른쪽이다.
        //
        // 자식을 왼쪽부터 이어 붙이지 않는 이유는 그것이 메뉴가 아니기 때문이다: 이어 붙이면
        // 단축키가 이름 바로 뒤에 오므로 줄마다 다른 자리에 서고, 훑어 내려가며 키를 찾을 수
        // 없다. 폭은 여전히 「이름 + 간격 + 단축키」로 <c>LayoutGroup</c>이 재며, 그 계산은
        // 자식이 어디 놓이는지와 무관하다 — 재는 것은 글자이지 사각형이 아니다.
        const auto spanTheRow = [](TextRenderer* const text)
        {
            if (!text || !text->GetGameObject())
            {
                return;
            }
            if (RectTransform* const rect = text->GetGameObject()->GetComponent<RectTransform>())
            {
                rect->SetAnchorMin({ 0.0f, 0.0f });
                rect->SetAnchorMax({ 1.0f, 1.0f });
                rect->SetOffsetMin({ MenuRowPadding, 0.0f });
                rect->SetOffsetMax({ -MenuRowPadding, 0.0f });
            }
        };

        const std::string label(item.label);
        row.label = AddLabel(scene, *row.object, rowName + " Label", label, TextColor, fontSize);
        spanTheRow(row.label);
        if (!item.shortcut.empty())
        {
            // 단축키가 있는 줄만 자식을 하나 더 갖는다. 빈 글자로 자식을 두면 간격이 한 번 더
            // 세어져, 단축키가 없는 줄이 있는 줄만큼 넓어진다.
            row.shortcut = AddLabel(
                scene, *row.object, rowName + " Shortcut", std::string(item.shortcut),
                DimTextColor, fontSize, TextRenderer::Alignment::Right);
            spanTheRow(row.shortcut);
        }
        list.rows.push_back(row);
    }
    return list;
}

void EditorMenuBarView::Build(Scene& scene, GameObject& parent, const float fontSize)
{
    mBar = BuildMenuBar(scene, parent, fontSize);
    if (!mBar.object)
    {
        return;
    }
    mLists.clear();
    mLists.reserve(GetEditorMenus().size());
    for (std::size_t index = 0; index < GetEditorMenus().size(); ++index)
    {
        // 목록은 막대가 아니라 막대의 부모에 매달린다. 막대는 가로 ContentFit이라 자식을
        // 머리줄로 세어 옆에 늘어놓으므로, 목록이 그 안에 있으면 네 번째 머리줄이 된다.
        MenuListParts list = BuildMenuList(scene, parent, index, fontSize);
        if (list.object)
        {
            list.object->SetActive(false);
        }
        mLists.push_back(std::move(list));
    }
}

const MenuListParts* EditorMenuBarView::GetOpenList() const
{
    const std::optional<std::size_t> open = mState.GetOpenMenu();
    if (!open.has_value() || *open >= mLists.size())
    {
        return nullptr;
    }
    return &mLists[*open];
}

std::optional<std::size_t> EditorMenuBarView::ClickedHeading() const
{
    for (std::size_t index = 0; index < mBar.headings.size(); ++index)
    {
        const Button* const button = mBar.headings[index].button;
        if (button && button->WasClickedThisFrame())
        {
            return index;
        }
    }
    return std::nullopt;
}

void EditorMenuBarView::PlaceOpenList(const float clientWidth, const float scale)
{
    const std::optional<std::size_t> open = mState.GetOpenMenu();
    for (std::size_t index = 0; index < mLists.size(); ++index)
    {
        MenuListParts& list = mLists[index];
        if (!list.object || !list.rect)
        {
            continue;
        }
        const bool isOpen = open.has_value() && *open == index;
        list.object->SetActive(isOpen);
        if (!isOpen || index >= mBar.headings.size() || !mBar.headings[index].rect)
        {
            continue;
        }

        const RectTransform::Rect& heading = mBar.headings[index].rect->GetResolvedRect();
        const RectTransform::Size& demand = list.rect->GetDesiredSize();
        const MenuListPlacement placement = PlaceMenuList(
            { heading.x, heading.y, heading.width, heading.height }, demand.width, demand.height,
            clientWidth, scale);

        // 목록은 부모의 왼쪽 위를 기준으로 놓인다. 앵커를 한 점에 모아 두었으므로 오프셋이
        // 곧 화면의 자리이며, 여기서 배율을 곱하지 않는다 — 자리는 이미 픽셀이다.
        list.rect->SetOffsetMin({ placement.panel.x, placement.panel.y });
        list.rect->SetOffsetMax({ placement.panel.x + placement.panel.width,
                                  placement.panel.y + placement.panel.height });

        // 창이 목록보다 좁으면 오른쪽이 잘린다. 반쯤 잘린 단축키는 잘못된 키로 읽히므로,
        // 그때는 아예 그리지 않는다.
        for (const MenuRowParts& row : list.rows)
        {
            if (row.shortcut && row.shortcut->GetGameObject())
            {
                row.shortcut->GetGameObject()->SetActive(placement.shortcutsFit);
            }
        }
    }
}

void EditorMenuBarView::RefreshRows(const MenuCommandAvailability& availability)
{
    const std::optional<std::size_t> open = mState.GetOpenMenu();

    // 열린 머리줄은 목록과 같은 색을 입는다. 목록과 그 머리줄이 한 덩어리로 보여야 무엇이
    // 열려 있는지 읽히기 때문이며, 그 색은 커서가 어디 있든 남아야 하므로 hover가 아니라
    // 평상시 색을 갈아 준다 — 커서가 목록으로 내려가면 hover는 머리줄을 떠난다.
    for (std::size_t index = 0; index < mBar.headings.size(); ++index)
    {
        Button* const button = mBar.headings[index].button;
        if (button)
        {
            button->SetNormalColor(open.has_value() && *open == index ? PanelColor
                                                                     : QuietHeading);
        }
    }

    if (!open.has_value() || *open >= mLists.size())
    {
        return;
    }
    for (const MenuRowParts& row : mLists[*open].rows)
    {
        if (row.isSeparator)
        {
            continue;
        }
        const bool enabled = IsEditorMenuCommandEnabled(row.command, availability);
        if (row.button)
        {
            // 받지 않게 하는 것이 곧 「고를 수 없다」이다. 이 한 줄로 그 줄은 커서를 가로채지
            // 않게 되고, 강조도 서지 않으며, 눌러도 아무 일이 없다 — 세 가지를 따로 끄면
            // 언젠가 그중 하나만 남는다.
            row.button->SetInteractable(enabled);
        }
        if (row.label)
        {
            // 고를 수 없는 줄은 사라지지 않고 흐려진다. 사라지면 사람은 그 기능이 없다고
            // 결론짓고, 흐리면 「지금은 아니다」를 읽는다.
            row.label->SetColor(enabled ? TextColor : DimTextColor);
        }

        // 정렬에는 정렬할 폭이 필요하다. 렌더러의 기본 최대 폭은 0이므로,
        // 오른쪽 정렬 단축키에도 줄의 실제 폭을 지정해야 한다.
        //
        // 줄의 폭은 목록이 놓이고 나서야 정해지므로 여기서 매 프레임 넘긴다. 툴바의 가운데
        // 정렬 라벨이 같은 이유로 같은 일을 한다.
        const float rowWidth = row.rect ? row.rect->GetResolvedRect().width : 0.0f;
        if (row.label)
        {
            row.label->SetMaxWidth(rowWidth);
        }
        if (row.shortcut)
        {
            row.shortcut->SetMaxWidth(rowWidth);
        }
    }
}

std::optional<EditorMenuCommand> EditorMenuBarView::Update(
    const MenuPointer& pointer, const MenuCommandAvailability& availability,
    const float clientWidth, const float scale)
{
    std::optional<EditorMenuCommand> chosen;
    bool tookTheClick = false;

    if (pointer.cancelled)
    {
        mState.Cancel();
    }
    else if (const std::optional<std::size_t> heading = ClickedHeading())
    {
        mState.PressHeading(*heading);
        tookTheClick = true;
    }
    else if (const MenuListParts* const list = GetOpenList())
    {
        for (const MenuRowParts& row : list->rows)
        {
            if (row.button && row.button->WasClickedThisFrame())
            {
                // 받지 않는 줄은 여기까지 오지 않는다 — 클릭은 그 줄을 뚫고 판으로 떨어진다.
                chosen = row.command;
                mState.ChooseItem();
                tookTheClick = true;
                break;
            }
        }
        // 줄 사이의 여백은 판이 받는다. 그 클릭은 목록을 겨눈 것이지 떠난 것이 아니므로
        // 아무 일도 일어나지 않되 메뉴는 열린 채로 남는다.
        if (!tookTheClick && list->blocker && list->blocker->WasClickedThisFrame())
        {
            tookTheClick = true;
        }
    }

    // 「밖」은 자리가 아니라 <b>남은 것</b>으로 정해진다: 클릭이 완성됐는데 내 요소 중 아무도
    // 그것을 받지 않았다면 그 클릭은 메뉴 밖에서 일어난 것이다. 좌표를 다시 재지 않으므로
    // 겹침·창·모달 규칙이 메뉴에도 그대로 적용된다.
    if (!pointer.cancelled && pointer.clicked && !tookTheClick)
    {
        mState.PressOutside();
    }

    PlaceOpenList(clientWidth, scale);
    RefreshRows(availability);
    return chosen;
}

}
