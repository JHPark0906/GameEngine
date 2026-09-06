#include "pch.h"
#include "RuntimeContext.h"
#include "GameObject.h"

#include "Transform.h"
#include "../Diagnostics/Debug.h"

#include <cassert>
#include <memory>
#include <ranges>
#include <string>
#include <utility>

namespace GameEngine::Runtime
{

GameObject::GameObject(RuntimeContext& runtimeContext, std::string name)
    : Object(runtimeContext.GetObjectRegistry(), std::move(name))
    , mRuntimeContext(&runtimeContext)
{
    mTransform = AddComponent<Transform>();
    assert(mTransform != nullptr && "Every GameObject requires a Transform.");
}

GameObject::~GameObject()
{
    mIsDestroying = true;
    for (const std::unique_ptr<Component>& component : mComponents)
    {
        component->Destroy();
    }
}

std::unique_ptr<GameObject> GameObject::Clone() const
{
    auto clonedGameObject = std::make_unique<GameObject>(*mRuntimeContext, GetName());
    clonedGameObject->SetActive(mIsActive);
    clonedGameObject->GetTransform().SetPosition(mTransform->GetPosition());
    clonedGameObject->GetTransform().SetRotation(mTransform->GetRotation());
    clonedGameObject->GetTransform().SetScale(mTransform->GetScale());

    for (const std::unique_ptr<Component>& component : mComponents)
    {
        if (component.get() == mTransform)
        {
            continue;
        }

        std::unique_ptr<Component> clonedComponent = component->Clone();
        if (!clonedComponent)
        {
            Diagnostics::Debug::LogWarning(
                "Skipped a component that does not support cloning on GameObject: " + GetName());
            continue;
        }

        clonedComponent->SetName(component->GetName());
        // enabled는 Behaviour의 속성이므로 기본 Clone이 다른 속성들과 함께 복사한다.
        (void)clonedGameObject->AddComponent(std::move(clonedComponent));
    }

    return clonedGameObject;
}

bool GameObject::IsActiveInHierarchy() const
{
    if (!mIsActive)
    {
        return false;
    }

    const Transform* parent = mTransform->GetParent();
    return !parent || !parent->GetGameObject() || parent->GetGameObject()->IsActiveInHierarchy();
}

void GameObject::Update(const float deltaTime)
{
    if (!IsActiveInHierarchy())
    {
        return;
    }

    mIsUpdating = true;
    const size_t componentCount = mComponents.size();

    for (size_t index = 0; index < componentCount; ++index)
    {
        Component* component = mComponents[index].get();
        if (IsPendingRemoval(component))
        {
            continue;
        }

        component->Tick(deltaTime);
    }

    mIsUpdating = false;
    FlushPendingRemovals();
}

Component* GameObject::AddComponent(std::unique_ptr<Component> component)
{
    if (mIsDestroying || !component || component->GetGameObject())
    {
        return nullptr;
    }

    if (Detail::IsComponentOfType<Transform>(*component) && mTransform)
    {
        Diagnostics::Debug::LogError("A GameObject cannot contain more than one Transform.");
        return nullptr;
    }

    // A component is registered here rather than in its own constructor, so that a component class
    // needs no registry argument and the components a project defines keep their plain constructors.
    component->RegisterWith(GetObjectRegistry());

    Component* rawPointer = component.get();
    mComponents.push_back(std::move(component));

    if (Detail::IsComponentOfType<Transform>(*rawPointer))
    {
        mTransform = static_cast<Transform*>(rawPointer);
    }

    const bool wasDispatchingLifecycle = mIsDispatchingLifecycle;
    mIsDispatchingLifecycle = true;
    rawPointer->AttachTo(*this);
    mIsDispatchingLifecycle = wasDispatchingLifecycle;

    const bool wasRemovedDuringAwake = IsPendingRemoval(rawPointer);
    if (!mIsUpdating && !mIsDispatchingLifecycle)
    {
        FlushPendingRemovals();
    }

    return wasRemovedDuringAwake ? nullptr : rawPointer;
}

bool GameObject::RemoveComponent(Component* component)
{
    if (mIsDestroying || !component || component == mTransform ||
        component->GetGameObject() != this || component->mDestroyed)
    {
        return false;
    }

    if (mIsUpdating || mIsDispatchingLifecycle)
    {
        if (!IsPendingRemoval(component))
        {
            mPendingComponentRemovals.push_back(component);
        }
        return true;
    }

    const bool wasDestroyed = DestroyComponent(component);
    FlushPendingRemovals();
    return wasDestroyed;
}

bool GameObject::IsPendingRemoval(const Component* component) const
{
    return std::ranges::find(mPendingComponentRemovals, component) != mPendingComponentRemovals.end();
}

void GameObject::FlushPendingRemovals()
{
    while (!mPendingComponentRemovals.empty())
    {
        Component* component = mPendingComponentRemovals.back();
        mPendingComponentRemovals.pop_back();
        (void)DestroyComponent(component);
    }
}

bool GameObject::DestroyComponent(Component* component)
{
    const auto iterator = std::ranges::find_if(
        mComponents,
        [component](const std::unique_ptr<Component>& item)
        {
            return item.get() == component;
        });

    if (iterator == mComponents.end())
    {
        return false;
    }

    // 사용자 OnDestroy가 같은 객체에 컴포넌트를 추가하면 벡터가 재할당될 수 있다.
    // 콜백 전에 소유권을 분리하고 iterator를 소비하여, 콜백을 건너 살아 있는 iterator를 없앤다.
    std::unique_ptr<Component> removed = std::move(*iterator);
    mComponents.erase(iterator);
    const bool wasDispatchingLifecycle = mIsDispatchingLifecycle;
    mIsDispatchingLifecycle = true;
    removed->Destroy();
    mIsDispatchingLifecycle = wasDispatchingLifecycle;
    return true;
}

}
