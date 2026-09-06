#include "pch.h"
#include "Transform.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "GameObject.h"
#include "PropertyDescriptor.h"
#include "../Diagnostics/Debug.h"

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// Transform이 선언하는 속성들이다. 이름은 장면 파일 형식이므로 기존 파일이 읽히는 그대로다.
    /// rotation의 상수 주석은 파일 속 회전이 어떤 표기인지를 파일 스스로 말하게 한다 — 값이
    /// 아니라 규약이라, 쓰기는 곁에 쓰고 읽기는 다른 표기를 거절한다.
    /// </summary>
    std::span<const PropertyDescriptor> TransformProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<Transform>(
                "position", "Position", &Transform::GetPosition, &Transform::SetPosition),
            MakeProperty<Transform>(
                "rotation", "Rotation", &Transform::GetRotation, &Transform::SetRotation,
                PropertyTraits::None,
                { { "rotationUnit", "degrees" }, { "rotationOrder", "rollPitchYaw" } }),
            MakeProperty<Transform>("scale", "Scale", &Transform::GetScale, &Transform::SetScale),
        };
        return properties;
    }
}

const ComponentType& Transform::StaticType()
{
    // 생성 훅은 없다: Transform은 GameObject의 일부라서 이름으로 따로 만들지 않는다.
    static const ComponentType type{
        "Transform", &Component::StaticType(), &TransformProperties };
    return type;
}
Transform::~Transform()
{
    if (mParent)
    {
        std::erase(mParent->mChildren, this);
        mParent = nullptr;
    }
    for (Transform* child : mChildren)
    {
        child->mParent = nullptr;
        // 부모를 잃은 자식의 월드는 이제 자기 로컬이다. 캐시가 남으면 옛 부모의 자취를 계속
        // 돌려준다.
        child->InvalidateWorldMatrix();
    }
    mChildren.clear();
}

void Transform::SetAsLastSibling()
{
    if (mParent)
    {
        SetSiblingIndex(mParent->mChildren.size());
    }
}

std::size_t Transform::GetSiblingIndex() const
{
    if (!mParent)
    {
        return 0;
    }
    const auto& siblings = mParent->mChildren;
    return static_cast<std::size_t>(
        std::find(siblings.begin(), siblings.end(), this) - siblings.begin());
}

void Transform::SetSiblingIndex(const std::size_t index)
{
    if (!mParent)
    {
        return;
    }
    auto& siblings = mParent->mChildren;
    const auto current = std::find(siblings.begin(), siblings.end(), this);
    if (current == siblings.end())
    {
        return;
    }
    const auto destination = siblings.begin() + std::min(index, siblings.size() - 1);
    if (current < destination)
    {
        std::rotate(current, current + 1, destination + 1);
    }
    else if (current > destination)
    {
        std::rotate(destination, current, current + 1);
    }
}

bool Transform::SetParent(Transform* parent, const bool worldPositionStays)
{
    if (parent == this || (parent && parent->IsDescendantOf(this)))
    {
        Diagnostics::Debug::LogError("Cannot create a circular Transform hierarchy.");
        return false;
    }

    if (parent && GetGameObject() && parent->GetGameObject() &&
        GetGameObject()->GetScene() && parent->GetGameObject()->GetScene() &&
        GetGameObject()->GetScene() != parent->GetGameObject()->GetScene())
    {
        Diagnostics::Debug::LogError("A Transform hierarchy cannot cross Scene boundaries.");
        return false;
    }

    if (mParent == parent)
    {
        return true;
    }

    // 월드를 유지하려면 옮기기 전의 월드 변환을 새 부모의 로컬로 다시 쓴다:
    // local = world * inverse(parentWorld). 새 부모가 없으면 월드가 곧 로컬이다.
    std::optional<Math::Matrix4x4> newLocal;
    if (worldPositionStays)
    {
        const Math::Matrix4x4 world = GetLocalToWorldMatrix();
        Math::Matrix4x4 parentInverse = Math::Matrix4x4::Identity();
        if (!parent || parent->GetLocalToWorldMatrix().TryInvert(parentInverse))
        {
            newLocal = world * parentInverse;
        }
        else
        {
            Diagnostics::Debug::LogWarning(
                "The new parent's transform is not invertible; local values are kept instead.");
        }
    }

    if (mParent)
    {
        std::erase(mParent->mChildren, this);
    }

    mParent = parent;
    if (mParent)
    {
        mParent->mChildren.push_back(this);
    }
    // 부모가 바뀌면 자신과 자손의 월드가 전부 다른 사슬 위에 선다.
    InvalidateWorldMatrix();

    if (newLocal)
    {
        SetFromLocalMatrix(*newLocal);
    }
    return true;
}

Math::Matrix4x4 Transform::GetLocalToWorldMatrix() const
{
    // 캐시는 로컬 값이나 부모가 바뀔 때 무효화된다. 부모의 월드를 재귀로 묻므로, dirty였던
    // 조상들도 이 한 번의 물음으로 함께 채워진다.
    if (mWorldMatrixDirty)
    {
        const Math::Matrix4x4 local =
            Math::Matrix4x4::CreateTransform(mPosition, mRotation, mScale);
        mCachedWorldMatrix = mParent ? local * mParent->GetLocalToWorldMatrix() : local;
        mWorldMatrixDirty = false;
    }
    return mCachedWorldMatrix;
}

void Transform::SetFromLocalMatrix(const Math::Matrix4x4& local)
{
    local.Decompose(mScale, mRotation, mPosition);
    InvalidateWorldMatrix();
}

void Transform::InvalidateWorldMatrix()
{
    if (mWorldMatrixDirty)
    {
        return;
    }
    mWorldMatrixDirty = true;
    for (Transform* child : mChildren)
    {
        child->InvalidateWorldMatrix();
    }
}

Math::Vector3 Transform::GetWorldPosition() const
{
    return GetLocalToWorldMatrix().TransformPoint({});
}

bool Transform::SetWorldPosition(const Math::Vector3& position)
{
    if (!mParent)
    {
        SetPosition(position);
        return true;
    }

    Math::Matrix4x4 parentInverse;
    if (!mParent->GetLocalToWorldMatrix().TryInvert(parentInverse))
    {
        // 스케일 0인 부모처럼 역행렬이 없는 경우다. 그때 월드 delta를 로컬 delta로 착각하면
        // 충돌 해소가 다른 축으로 새므로, 호출자가 이번 이동을 건너뛰게 한다.
        return false;
    }
    SetPosition(parentInverse.TransformPoint(position));
    return true;
}

bool Transform::IsDescendantOf(const Transform* transform) const
{
    for (const Transform* current = mParent; current; current = current->mParent)
    {
        if (current == transform)
        {
            return true;
        }
    }
    return false;
}

std::unique_ptr<Component> Transform::Clone() const
{
    auto transform = std::make_unique<Transform>();
    transform->mPosition = mPosition;
    transform->mRotation = mRotation;
    transform->mScale = mScale;
    return transform;
}

void Transform::OnRemoved()
{
    (void)SetParent(nullptr);
    const std::vector<Transform*> children = mChildren;
    for (Transform* child : children)
    {
        (void)child->SetParent(nullptr);
    }
}

}
