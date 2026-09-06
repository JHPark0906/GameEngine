#include "pch.h"
#include "Button.h"

#include "GameObject.h"
#include "PropertyDescriptor.h"
#include "SpriteRenderer.h"

#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// Button이 선언하는 속성들이다. 직렬화 키, 인스펙터 행의 유일한 출처다. 상태
    /// (hover/press/click)는 매 프레임 이벤트 시스템이 다시 정하는 것이라 여기 없다 — 파일에
    /// 적어 두면 다음 로드가 커서 아래에 있지도 않은 버튼을 눌린 채로 되살린다.
    /// </summary>
    std::span<const PropertyDescriptor> ButtonProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<Button>(
                "normalColor", "Normal Color", &Button::GetNormalColor, &Button::SetNormalColor),
            MakeProperty<Button>(
                "hoveredColor", "Hovered Color", &Button::GetHoveredColor,
                &Button::SetHoveredColor),
            MakeProperty<Button>(
                "pressedColor", "Pressed Color", &Button::GetPressedColor,
                &Button::SetPressedColor),
            MakeProperty<Button>(
                "disabledColor", "Disabled Color", &Button::GetDisabledColor,
                &Button::SetDisabledColor),
        };
        return properties;
    }
}

const ComponentType& Button::StaticType()
{
    static const ComponentType type{
        "Button", &Selectable::StaticType(), &ButtonProperties,
        &MakeComponentInstance<Button> };
    return type;
}

void Button::OnInteractableLost()
{
    // 잡고 있던 눌림은 이벤트 시스템이 다음 프레임에 놓는다 — 여기서는 보이는 상태만
    // 정리한다. 클릭까지 지우는 것은 받지 않게 된 버튼이 눌림을 들고 있다가 나중에
    // 배달하면 안 되기 때문이다. 받는 동안 일어난 클릭은 이번 프레임의 사실이라
    // 여기서 지우지 않고, 다음 프레임에 이벤트 시스템이 덮는다.
    mHovered = false;
    mPressed = false;
    mClicked = false;
    // 쥔 것도 함께 놓는다. 받지 않게 된 버튼이 포인터를 쥔 채로 남으면 그 끌기는 놓을 수
    // 없는 것이 된다.
    mHoldsPointer = false;
    ApplyPointerState(mHovered, mPressed, mClicked, mHoldsPointer);
}

void Button::ApplyPointerState(
    const bool hovered, const bool pressed, const bool clicked, const bool holdsPointer)
{
    mHovered = hovered;
    mPressed = pressed;
    mClicked = clicked;
    mHoldsPointer = holdsPointer;

    // 색을 입히는 것은 같은 오브젝트의 SpriteRenderer가 있을 때뿐이다. 없으면 상태만 남고,
    // 그것을 읽어 무엇을 할지는 스크립트의 몫이다 — 버튼이 자기 그림을 만들지는 않는다.
    GameObject* const owner = GetGameObject();
    SpriteRenderer* const sprite = owner ? owner->GetComponent<SpriteRenderer>() : nullptr;
    if (!sprite)
    {
        return;
    }
    if (!IsInteractable())
    {
        sprite->SetColor(mDisabledColor);
        return;
    }
    sprite->SetColor(mPressed ? mPressedColor : (mHovered ? mHoveredColor : mNormalColor));
}

}
