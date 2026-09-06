#include "pch.h"
#include "Behaviour.h"

#include "GameObject.h"
#include "PropertyDescriptor.h"

#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// Behaviour가 선언하는 속성이다. 사슬 열람이 기반을 먼저 놓으므로, 여기 한 번 선언된
    /// enabled가 Camera·Light·렌더러 모든 파생의 속성 목록 맨 앞에 나타난다.
    /// </summary>
    std::span<const PropertyDescriptor> BehaviourProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<Behaviour>(
                "enabled", "Enabled", &Behaviour::IsEnabled, &Behaviour::SetEnabled),
        };
        return properties;
    }
}

const ComponentType& Behaviour::StaticType()
{
    static const ComponentType type{
        "Behaviour", &Component::StaticType(), &BehaviourProperties };
    return type;
}
bool Behaviour::IsActiveAndEnabled() const
{
    return mEnabled && GetGameObject() && GetGameObject()->IsActiveInHierarchy();
}

void Behaviour::UpdateComponent(const float deltaTime)
{
    if (IsActiveAndEnabled())
    {
        UpdateBehaviour(deltaTime);
    }
}

}
