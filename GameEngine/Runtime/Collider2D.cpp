#include "pch.h"
#include "Collider2D.h"

#include <algorithm>
#include <span>
#include <utility>

#include "GameObject.h"
#include "PropertyDescriptor.h"
#include "Transform.h"

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// Collider2D가 선언하는 속성이다. 겹침 목록은 여기 없다: 매 프레임 시스템이 다시 채우는
    /// 것이라, 파일에 적으면 다음 로드가 아무것도 닿지 않은 콜라이더를 겹친 채로 되살린다.
    /// </summary>
    std::span<const PropertyDescriptor> Collider2DProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<Collider2D>(
                "isTrigger", "Is Trigger", &Collider2D::IsTrigger, &Collider2D::SetTrigger),
        };
        return properties;
    }
}

const ComponentType& Collider2D::StaticType()
{
    static const ComponentType type{
        "Collider2D", &Behaviour::StaticType(), &Collider2DProperties };
    return type;
}

bool Collider2D::IsOverlapping(const unsigned int instanceId) const
{
    return std::ranges::find(mOverlapping, instanceId) != mOverlapping.end();
}

Math::Aabb2D Collider2D::TransformToWorld(const Math::Aabb2D& localBounds) const
{
    const GameObject* const owner = GetGameObject();
    if (!owner || localBounds.IsEmpty())
    {
        return localBounds;
    }
    const Math::Matrix4x4 localToWorld = owner->GetTransform().GetLocalToWorldMatrix();
    const Math::Vector2& min = localBounds.min;
    const Math::Vector2& max = localBounds.max;
    const Math::Vector3 corners[] = {
        { min.GetX(), min.GetY(), 0.0f },
        { max.GetX(), min.GetY(), 0.0f },
        { min.GetX(), max.GetY(), 0.0f },
        { max.GetX(), max.GetY(), 0.0f },
    };

    const Math::Vector3 first = localToWorld.TransformPoint(corners[0]);
    Math::Aabb2D world{ { first.GetX(), first.GetY() }, { first.GetX(), first.GetY() } };
    for (std::size_t index = 1; index < std::size(corners); ++index)
    {
        const Math::Vector3 point = localToWorld.TransformPoint(corners[index]);
        world.min = {
            (std::min)(world.min.GetX(), point.GetX()), (std::min)(world.min.GetY(), point.GetY())
        };
        world.max = {
            (std::max)(world.max.GetX(), point.GetX()), (std::max)(world.max.GetY(), point.GetY())
        };
    }
    return world;
}

Math::Aabb2D Collider2D::TransformToLocal(const Math::Aabb2D& worldBox) const
{
    const GameObject* const owner = GetGameObject();
    if (!owner || worldBox.IsEmpty())
    {
        return worldBox;
    }
    // 월드에서 로컬로 가는 데 필요한 것은 위치와 크기다. 회전이 있으면 이 되돌림은 근사이며,
    // 그것이 이 컴포넌트가 축 정렬이라고 말하는 바로 그 뜻이다.
    const Math::Vector3 worldOrigin = owner->GetTransform().GetWorldPosition();
    const Math::Matrix4x4 localToWorld = owner->GetTransform().GetLocalToWorldMatrix();
    const Math::Vector3 unitX = localToWorld.TransformPoint({ 1.0f, 0.0f, 0.0f });
    const Math::Vector3 unitY = localToWorld.TransformPoint({ 0.0f, 1.0f, 0.0f });
    const float scaleX = unitX.GetX() - worldOrigin.GetX();
    const float scaleY = unitY.GetY() - worldOrigin.GetY();
    if (scaleX == 0.0f || scaleY == 0.0f)
    {
        return Math::Aabb2D{};
    }

    const Math::Vector2 origin{ worldOrigin.GetX(), worldOrigin.GetY() };
    const Math::Vector2 first{
        (worldBox.min.GetX() - origin.GetX()) / scaleX,
        (worldBox.min.GetY() - origin.GetY()) / scaleY
    };
    const Math::Vector2 second{
        (worldBox.max.GetX() - origin.GetX()) / scaleX,
        (worldBox.max.GetY() - origin.GetY()) / scaleY
    };
    return Math::Aabb2D::FromPoints(first, second);
}

std::optional<Math::Aabb2DSweepHit> Collider2D::SweepBox(
    const Math::Aabb2D& movingBox, const Math::Vector2& worldDisplacement) const
{
    return movingBox.SweepAgainst(GetWorldBounds(), worldDisplacement);
}

void Collider2D::ApplyOverlaps(std::vector<unsigned int> overlapping)
{
    std::ranges::sort(overlapping);
    for (const unsigned int id : overlapping)
    {
        if (std::ranges::find(mOverlapping, id) == mOverlapping.end())
        {
            mEntered.push_back(id);
        }
    }
    for (const unsigned int id : mOverlapping)
    {
        if (std::ranges::find(overlapping, id) == overlapping.end())
        {
            mExited.push_back(id);
        }
    }
    mOverlapping = std::move(overlapping);
}

void Collider2D::ClearFrameFlags()
{
    mEntered.clear();
    mExited.clear();
}

}
