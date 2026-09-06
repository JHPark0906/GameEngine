#include "Views/EditorToolbarButton.h"

#include "Rules/EditorPanelCommon.h"

#include "Assets/AssetReference.h"
#include "Runtime/Button.h"
#include "Runtime/GameObject.h"
#include "Runtime/LayoutElement.h"
#include "Runtime/LayoutGroup.h"
#include "Runtime/RectTransform.h"
#include "Runtime/Scene.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"

namespace GameEditor
{

namespace
{
    using GameEngine::Math::Vector2;
    using GameEngine::Runtime::Button;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::LayoutElement;
    using GameEngine::Runtime::LayoutGroup;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::SpriteRenderer;
    using GameEngine::Runtime::TextRenderer;

    /// <summary>버튼의 바탕 그림이다. 9-슬라이스라 어느 폭으로 늘려도 모서리가 유지된다.</summary>
    constexpr const char* ButtonSprite = "Sprites/button-32.png";

    /// <summary>이 오브젝트 아래에 사각형 하나를 만든다. 배경도 글자도 없는 자리다.</summary>
    [[nodiscard]] GameObject* AddRect(
        Scene& scene, GameObject& parent, const std::string& name, const Vector2& anchorMin,
        const Vector2& anchorMax, const Vector2& offsetMin, const Vector2& offsetMax)
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
}

ToolbarButtonParts BuildToolbarButton(
    Scene& scene, GameObject& parent, const std::string& label, const float fontSize)
{
    ToolbarButtonParts parts;
    // 자리도 폭도 여기서 정하지 않는다. 폭은 글자에서 나오고 자리는 접힘 계산이 정하므로,
    // 여기서 두는 사각형은 그 둘이 답하기 전의 모습뿐이다.
    parts.object = AddRect(
        scene, parent, label, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f },
        { MinimumToolbarButtonWidth, ToolbarLayoutMetrics{}.rowHeight });
    if (!parts.object)
    {
        return parts;
    }
    parts.rect = parts.object->GetComponent<RectTransform>();

    // 그림의 채움이 흰색이라 틴트 × 그림이 곧 틴트다. 그래서 버튼의 상태 색은 바탕 한가운데를
    // 지나며 값이 변하지 않고, 상태 사이의 거리도 선언한 그대로 남는다.
    if (SpriteRenderer* const background = parts.object->AddComponent<SpriteRenderer>())
    {
        background->SetSprite(GameEngine::Assets::AssetReference::Parse(ButtonSprite));
        background->SetDrawMode(SpriteRenderer::DrawMode::Sliced);
        // 사각형은 RectTransform이 정했지만, 그 사각형을 쓰겠다고 말하는 것은 이 한 줄이다.
        background->SetSpace(SpriteRenderer::Space::Screen);
    }
    parts.button = parts.object->AddComponent<Button>();

    // 버튼의 폭은 자기 글자에서 나온다. 글자는 자식 사각형에 살고 있으므로, 버튼은 자식이
    // 요구한 크기를 받는 컨테이너가 되고 글자 쪽이 자기 글자를 잰다 — 버튼 자신에게는 글자가
    // 없으니 LayoutElement를 여기 달면 잴 것이 없다.
    if (LayoutGroup* const group = parts.object->AddComponent<LayoutGroup>())
    {
        group->SetDirection(LayoutGroup::Direction::Horizontal);
        group->SetPadding(ToolbarLabelLeftInset);
    }

    // 글자는 버튼의 자식 사각형에 산다. 왼쪽 여백이 그 사각형의 몫이기 때문이다 — 버튼 사각형을
    // 그대로 쓰면 글자가 테두리에 붙는다. 왼쪽 정렬인 이유는 가운데 정렬이 「주어진 폭 안에서
    // 가운데」라 폭 선언이 조금만 어긋나도 글자가 버튼 밖으로 밀리기 때문이다.
    GameObject* const labelObject = AddRect(
        scene, *parts.object, label + " Label", { 0.0f, 0.0f }, { 1.0f, 1.0f },
        { ToolbarLabelLeftInset, 0.0f }, { 0.0f, 0.0f });
    if (!labelObject)
    {
        return parts;
    }
    parts.labelRect = labelObject->GetComponent<RectTransform>();
    parts.label = labelObject->AddComponent<TextRenderer>();
    if (!parts.label)
    {
        return parts;
    }
    parts.label->SetText(label);
    parts.label->SetColor(TextColor);
    parts.label->SetSpace(TextRenderer::Space::Screen);
    parts.label->SetAlignment(TextRenderer::Alignment::Left);
    // 세로 가운데는 TextRenderer가 잡는다. 여기서 어림할 수 없는 값이다 — 가운데를 잡으려면
    // 배치된 글자 블록의 높이를 알아야 하고, 그것은 글꼴과 크기가 정한다.
    parts.label->SetVerticalAlignment(TextRenderer::VerticalAlignment::Middle);
    parts.label->SetFontSize(fontSize);

    // 글자를 가진 쪽이 자기 글자를 잰다. 이 답이 부모인 버튼에게 올라가 버튼의 폭이 된다.
    if (LayoutElement* const element = labelObject->AddComponent<LayoutElement>())
    {
        element->SetFit(LayoutElement::Fit::Horizontal);
        // 아직 재지 못한 프레임과 글꼴을 열지 못한 화면의 바닥이다.
        element->SetMinimumSize(
            { MinimumToolbarButtonWidth - ToolbarLabelLeftInset * 2.0f, 0.0f });
    }
    return parts;
}

}
