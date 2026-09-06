#include "pch.h"
#include "BoxCollider3D.h"

#include <span>

#include "PropertyDescriptor.h"

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>BoxCollider3D가 선언하는 속성들이다. 직렬화 키이자 인스펙터 행의 출처다.</summary>
    std::span<const PropertyDescriptor> BoxCollider3DProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<BoxCollider3D>(
                "offset", "Offset", &BoxCollider3D::GetOffset, &BoxCollider3D::SetOffset),
            MakeProperty<BoxCollider3D>(
                "size", "Size", &BoxCollider3D::GetSize, &BoxCollider3D::SetSize),
        };
        return properties;
    }
}

const ComponentType& BoxCollider3D::StaticType()
{
    static const ComponentType type{
        "BoxCollider3D", &Collider3D::StaticType(), &BoxCollider3DProperties,
        &MakeComponentInstance<BoxCollider3D> };
    return type;
}

Math::Aabb3D BoxCollider3D::GetWorldBounds() const
{
    if (mSize.GetX() <= 0.0f || mSize.GetY() <= 0.0f || mSize.GetZ() <= 0.0f)
    {
        return Math::Aabb3D::Empty();
    }
    return TransformToWorld(Math::Aabb3D::FromCenterSize(mOffset, mSize));
}

}
