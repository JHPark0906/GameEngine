#include "pch.h"
#include "Component.h"

#include "GameObject.h"
#include "PropertyDescriptor.h"
#include "Transform.h"
#include "../Diagnostics/Debug.h"

#include <cassert>
#include <memory>
#include <utility>

namespace GameEngine::Runtime
{

const ComponentType& Component::StaticType()
{
    static const ComponentType type{ "Component", nullptr };
    return type;
}

std::unique_ptr<Component> Component::Clone() const
{
    const ComponentType& type = GetComponentType();
    std::unique_ptr<Component> clone = type.CreateInstance();
    if (!clone)
    {
        return nullptr;
    }
    const PropertyRestoreScope restore(*clone);
    // 서술자를 소유한 타입의 인스턴스끼리의 복사라 TrySet은 실패할 수 없다. 실패한다면 서술
    // 자체의 버그이므로, 조용히 값을 빼놓는 대신 로그로 말한다.
    for (const PropertyDescriptor* const descriptor : CollectProperties(type))
    {
        if (!descriptor->TrySet(*clone, descriptor->Get(*this)))
        {
            Diagnostics::Debug::LogError(
                "A property could not be copied to a clone. type=", type.GetName(),
                ", property=", descriptor->GetName());
        }
    }

    // 컴포넌트의 상태는 "선언된 속성 + 속성 밖 상태"다. 속성만 복사하면 타일맵의 격자처럼
    // 구조로 실린 상태가 조용히 빠진 복제본이 된다. 여기서 직렬화와 같은 왕복을 하므로, 새
    // 컴포넌트는 복제를 위해 따로 쓸 코드가 없다. 쓸 것이 없는 컴포넌트는 왕복 자체를 건너뛴다.
    Core::Json::Object extra;
    WriteExtraSerializedState(extra);
    if (!extra.empty())
    {
        clone->ReadExtraSerializedState(Core::Json(std::move(extra)));
    }
    return clone;
}
void Component::AttachTo(GameObject& gameObject)
{
    assert(mGameObject == nullptr && "Component is already attached to a GameObject.");
    mGameObject = &gameObject;
    OnAttached();
}

void Component::Tick(const float deltaTime)
{
    UpdateComponent(deltaTime);
}

void Component::Destroy()
{
    if (mDestroyed)
    {
        return;
    }

    mDestroyed = true;
    OnRemoved();
    mGameObject = nullptr;
}

Transform* Component::GetTransform() const
{
    return mGameObject ? &mGameObject->GetTransform() : nullptr;
}

Scene* Component::GetScene() const
{
    return mGameObject ? mGameObject->GetScene() : nullptr;
}

RuntimeContext* Component::GetRuntimeContext() const
{
    return mGameObject ? &mGameObject->GetRuntimeContext() : nullptr;
}

}
