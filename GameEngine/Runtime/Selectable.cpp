#include "pch.h"
#include "Selectable.h"

#include "RectTransform.h"

#include "GameObject.h"
#include "PropertyDescriptor.h"

#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// 커서를 받는 요소가 공통으로 갖는 속성이다. 부류마다 적어 두면 같은 키가 셋이 되고,
    /// 하나를 고칠 때 나머지 둘이 조용히 갈라진다. 직렬화와 인스펙터와 스키마가 모두 사슬을
    /// 걷는 <c>CollectProperties</c>를 읽으므로, 여기 한 번 적으면 셋 모두가 갖는다.
    /// </summary>
    std::span<const PropertyDescriptor> SelectableProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<Selectable>(
                "interactable", "Interactable", &Selectable::IsInteractable,
                &Selectable::SetInteractable),
        };
        return properties;
    }

    /// <summary>
    /// 화면 사각형 없이는 이 요소들이 아무 일도 하지 않는다. 배치도 히트 테스트도 사각형으로
    /// 요소를 찾으므로, 사각형이 없으면 소리 없이 죽는다 — 붙일 때 함께 붙는 편이 낫다.
    /// </summary>
    [[nodiscard]] std::span<const ComponentType* const> SelectableRequirements()
    {
        static const ComponentType* const required[]{ &RectTransform::StaticType() };
        return required;
    }
}

const ComponentType& Selectable::StaticType()
{
    // 생성 훅이 비어 있다 — 이 이름으로는 붙일 수 없다. Behaviour와 같은 자리다.
    static const ComponentType type{
        "Selectable", &Behaviour::StaticType(), &SelectableProperties, nullptr,
        &SelectableRequirements };
    return type;
}

void Selectable::SetInteractable(const bool interactable)
{
    mInteractable = interactable;
    if (!mInteractable)
    {
        OnInteractableLost();
    }
}

bool Selectable::Covers(const float x, const float y) const
{
    const GameObject* const owner = GetGameObject();
    const RectTransform* const rectTransform =
        owner ? owner->GetComponent<RectTransform>() : nullptr;
    return rectTransform && rectTransform->GetVisibleRect().Contains(x, y);
}

}
