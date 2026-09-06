#include "pch.h"
#include "BoxCollider2D.h"

#include <span>

#include "PropertyDescriptor.h"

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>BoxCollider2D가 선언하는 속성들이다. 직렬화 키이자 인스펙터 행의 유일한 출처다.</summary>
    std::span<const PropertyDescriptor> BoxCollider2DProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<BoxCollider2D>(
                "offset", "Offset", &BoxCollider2D::GetOffset, &BoxCollider2D::SetOffset),
            MakeProperty<BoxCollider2D>(
                "size", "Size", &BoxCollider2D::GetSize, &BoxCollider2D::SetSize),
        };
        return properties;
    }
}

const ComponentType& BoxCollider2D::StaticType()
{
    static const ComponentType type{
        "BoxCollider2D", &Collider2D::StaticType(), &BoxCollider2DProperties,
        &MakeComponentInstance<BoxCollider2D> };
    return type;
}

Core::Aabb2D BoxCollider2D::GetWorldBounds() const
{
    if (mSize.GetX() <= 0.0f || mSize.GetY() <= 0.0f)
    {
        return Core::Aabb2D{};
    }
    return TransformToWorld(Core::Aabb2D::FromCenterSize(mOffset, mSize));
}

}
