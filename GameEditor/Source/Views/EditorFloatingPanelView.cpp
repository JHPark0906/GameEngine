#include "Views/EditorFloatingPanelView.h"

#include <algorithm>

#include "Rules/EditorPanelCommon.h"
#include "Assets/AssetReference.h"
#include "Diagnostics/Debug.h"
#include "Runtime/Button.h"
#include "Runtime/Canvas.h"
#include "Runtime/GameObject.h"
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
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::SpriteRenderer;
    using GameEngine::Runtime::TextRenderer;

    /// <summary>
    /// 창의 바탕 그림이다. 툴바 띠와 같은 9-슬라이스라, 떠 있는 창이 툴바와 같은 재질로 읽힌다.
    /// </summary>
    constexpr const char* PanelSprite = "Sprites/panel-32.png";

    /// <summary>제목 글자가 왼쪽 변에서 들여지는 거리다.</summary>
    constexpr float TitleInset = 8.0f;

    /// <summary>도킹으로 돌려보내는 버튼의 폭이다. 논리 픽셀이다.</summary>
    constexpr float DockButtonWidth = 44.0f;

    /// <summary>부모 아래에 사각형 하나를 만든다. 자리는 앵커와 오프셋이 정한다.</summary>
    [[nodiscard]] GameObject* AddRect(
        GameEngine::Runtime::Scene& scene, GameObject& parent, const std::string& name,
        const Vector2& anchorMin, const Vector2& anchorMax, const Vector2& offsetMin,
        const Vector2& offsetMax)
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
        rect->SetAnchorMin(anchorMin);
        rect->SetAnchorMax(anchorMax);
        rect->SetOffsetMin(offsetMin);
        rect->SetOffsetMax(offsetMax);
        return object;
    }

    void AddSlicedBackground(GameObject& object, const GameEngine::Math::Color& color)
    {
        if (SpriteRenderer* const background = object.AddComponent<SpriteRenderer>())
        {
            background->SetSprite(GameEngine::Assets::AssetReference::Parse(PanelSprite));
            background->SetDrawMode(SpriteRenderer::DrawMode::Sliced);
            // 사각형은 RectTransform이 정했지만, 그 사각형을 쓰겠다고 말하는 것은 이 한 줄이다.
            background->SetSpace(SpriteRenderer::Space::Screen);
            background->SetColor(color);
        }
    }
}


void EditorFloatingPanelView::Build(
    GameEngine::Runtime::Scene& scene, GameEngine::Runtime::GameObject& layer,
    const std::string& title)
{
    // 창들은 모두 한 층의 자식이다. 루트와 서로 다른 캔버스 사이에서는 형제 순서로
    // 앞뒤를 정할 수 없으므로, 같은 부모 아래에서 계층 순서로 창의 겹침을 관리한다.
    mLayerObject = &layer;

    // 자리는 매 프레임 Synchronize가 세운다. 여기서는 앵커만 좌상단으로 고정해 둔다 — 창은
    // 화면 크기를 따라 늘어나는 것이 아니라 자기 자리를 들고 있는 것이다.
    mWindowObject = AddRect(
        scene, layer, "FloatingPanel", { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f },
        { mRect.width, mRect.height });
    if (!mWindowObject)
    {
        GameEngine::Diagnostics::Debug::LogError("The editor could not create a floating panel.");
        return;
    }
    mWindowRect = mWindowObject->GetComponent<RectTransform>();
    mWindow = mWindowObject->AddComponent<GameEngine::Runtime::UIWindow>();
    // 몸통 바탕은 여기서 그리지 않는다. 유지 모드는 즉시 모드 <b>전체</b> 위에 그려지므로,
    // 창에 바탕을 붙이면 그 바탕이 자기 몸통 — 즉시 모드로 그려지는 로그 — 을 덮어 창이 빈
    // 채로 보인다. 그래서 유지 모드가 맡는 것은 제목줄까지이고, 몸통의 바탕은 몸통을 그리는
    // 쪽이 같은 층에서 함께 그린다.

    // 제목줄은 창 위쪽에 가로로 꽉 찬 띠다. 여기에 버튼을 두는 이유는 잡기 위해서다 — 눌림이
    // 시작된 자리가 이 띠라는 것을 알아야 끌기가 창의 것이 되고, 그 눌림이 아래로 새지 않는다.
    GameObject* const header = AddRect(
        scene, *mWindowObject, "FloatingPanelHeader", { 0.0f, 0.0f }, { 1.0f, 0.0f },
        { 0.0f, 0.0f }, { 0.0f, HeaderHeight });
    if (header)
    {
        AddSlicedBackground(*header, HeaderColor);
        mTitleBar = header->AddComponent<Button>();
        mHeaderRect = header->GetComponent<RectTransform>();

        // 도킹으로 돌려보내는 버튼이다. 제목줄 오른쪽 끝에 붙어, 창을 띄운 사람이 되돌리는
        // 길을 같은 자리에서 찾는다.
        GameObject* const dock = AddRect(
            scene, *header, "FloatingPanelDock", { 1.0f, 0.0f }, { 1.0f, 1.0f },
            { -DockButtonWidth, 0.0f }, { 0.0f, 0.0f });
        if (dock)
        {
            AddSlicedBackground(*dock, HeaderColor);
            mDockButton = dock->AddComponent<Button>();
            mDockRect = dock->GetComponent<RectTransform>();
            GameObject* const dockLabel = AddRect(
                scene, *dock, "FloatingPanelDockLabel", { 0.0f, 0.0f }, { 1.0f, 1.0f },
                { 0.0f, 0.0f }, { 0.0f, 0.0f });
            if (dockLabel)
            {
                mDockLabel = dockLabel->AddComponent<TextRenderer>();
                if (mDockLabel)
                {
                    mDockLabel->SetText("Dock");
                    mDockLabel->SetColor(TextColor);
                    mDockLabel->SetSpace(TextRenderer::Space::Screen);
                    mDockLabel->SetAlignment(TextRenderer::Alignment::Center);
                    mDockLabel->SetVerticalAlignment(TextRenderer::VerticalAlignment::Middle);
                    mDockLabel->SetFontSize(SecondaryFontSize);
                }
            }
        }

        GameObject* const label = AddRect(
            scene, *header, "FloatingPanelTitle", { 0.0f, 0.0f }, { 1.0f, 1.0f },
            { TitleInset, 0.0f }, { -DockButtonWidth - TitleInset, 0.0f });
        if (label)
        {
            mTitleLabelRect = label->GetComponent<RectTransform>();
            mTitleLabel = label->AddComponent<TextRenderer>();
            if (mTitleLabel)
            {
                mTitleLabel->SetText(title);
                mTitleLabel->SetColor(TextColor);
                mTitleLabel->SetSpace(TextRenderer::Space::Screen);
                mTitleLabel->SetAlignment(TextRenderer::Alignment::Left);
                mTitleLabel->SetVerticalAlignment(TextRenderer::VerticalAlignment::Middle);
                mTitleLabel->SetFontSize(RowFontSize);
            }
        }
    }

    // 처음에는 도킹돼 있다. 껍데기는 사람이 창을 띄울 때 켜진다.
    mWindowObject->SetActive(false);
}

void EditorFloatingPanelView::SetVisible(const bool visible)
{
    mVisible = visible;
    if (mWindowObject)
    {
        mWindowObject->SetActive(visible);
    }
}

void EditorFloatingPanelView::Synchronize(const float contentScale)
{
    const float scale = contentScale > 0.0f ? contentScale : 1.0f;

    // 제목줄이 포인터를 쥐기 시작하는 모서리다. 쥔 상태를 그대로 쓰면 누르고 있는 매 프레임이
    // 새 끌기의 시작으로 읽힌다. 세운 신호는 <b>지우지 않는다</b> — 가져가는 쪽이 지운다. 이
    // 함수가 한 프레임에 두 번 돌아도 첫 번째의 모서리가 살아 있어야 하기 때문이다.
    //
    // 눌림이 아니라 <b>쥠</b>을 보는 이유가 이 끌기의 전부다. 창이 움직이면 제목줄도 함께
    // 움직여 커서가 그 밖으로 나가고, 눌림은 그 프레임에 풀린다 — 눌림으로 읽으면 한 몸짓이
    // 프레임마다 끝나고 다시 시작해서, 창이 커서를 따라오지 않고 토막토막 뛴다.
    const bool pressed = mVisible && mTitleBar && mTitleBar->HoldsPointer();
    if (pressed && !mTitleWasPressed)
    {
        mTitlePressStarted = true;
    }
    mTitleWasPressed = pressed;

    if (!mVisible || !mWindowRect)
    {
        return;
    }

    // 배율은 이 층의 캔버스에 한 번만 적용한다. 오프셋에 미리 곱하면 상자만 커지고
    // 글자는 캔버스의 배율 1을 따라 작은 크기로 남는다.
    //
    // 층은 떠 있는 창들이 함께 쓰지만 모두 같은 화면의 같은 배율을 받으므로, 어느 창이
    // 적어도 값은 같다.
    if (mLayerObject)
    {
        if (GameEngine::Runtime::Canvas* const canvas =
                mLayerObject->GetComponent<GameEngine::Runtime::Canvas>())
        {
            canvas->SetScaleFactor(scale);
        }
    }

    // 창의 자리를 사각형에 싣는 유일한 자리다. 앵커는 좌상단에 고정돼 있으므로 오프셋 둘이
    // 곧 창의 네 값이며, 여기 말고 어디에서도 사각형 규약을 다시 세우지 않는다.
    //
    // 오프셋은 <b>논리 픽셀 그대로</b>다. 배율은 배치가 곱한다.
    mWindowRect->SetAnchorMin({ 0.0f, 0.0f });
    mWindowRect->SetAnchorMax({ 0.0f, 0.0f });
    mWindowRect->SetOffsetMin({ mRect.x, mRect.y });
    mWindowRect->SetOffsetMax({ mRect.x + mRect.width, mRect.y + mRect.height });

    if (mHeaderRect)
    {
        mHeaderRect->SetOffsetMin({ 0.0f, 0.0f });
        mHeaderRect->SetOffsetMax({ 0.0f, HeaderHeight });
    }
    if (mDockRect)
    {
        mDockRect->SetOffsetMin({ -DockButtonWidth, 0.0f });
        mDockRect->SetOffsetMax({ 0.0f, 0.0f });
    }
    if (mTitleLabelRect)
    {
        mTitleLabelRect->SetOffsetMin({ TitleInset, 0.0f });
        mTitleLabelRect->SetOffsetMax({ -DockButtonWidth - TitleInset, 0.0f });
    }

    if (mTitleLabel)
    {
        // 글꼴 크기도 논리 단위다. 캔버스가 배율을 쥐고 있으므로 여기서 곱하지 않는다.
        mTitleLabel->SetFontSize(RowFontSize);
    }
    if (mDockLabel)
    {
        mDockLabel->SetFontSize(SecondaryFontSize);
    }
}

bool EditorFloatingPanelView::ConsumeTitleBarPressStart()
{
    const bool started = mTitlePressStarted;
    mTitlePressStarted = false;
    return started;
}

bool EditorFloatingPanelView::WasDockRequested() const
{
    return mVisible && mDockButton && mDockButton->WasClickedThisFrame();
}

GameEngine::UI::UIRect EditorFloatingPanelView::GetContentRect(const float contentScale) const
{
    const float scale = contentScale > 0.0f ? contentScale : 1.0f;
    const float header = HeaderHeight * scale;
    return {
        mRect.x * scale, mRect.y * scale + header, mRect.width * scale,
        mRect.height * scale - header };
}

void EditorFloatingPanelView::Raise()
{
    if (mWindowObject)
    {
        mWindowObject->GetTransform().SetAsLastSibling();
    }
}

void EditorFloatingPanelView::SetModal(const bool modal)
{
    if (mWindow)
    {
        mWindow->SetModal(modal);
    }
}

}
