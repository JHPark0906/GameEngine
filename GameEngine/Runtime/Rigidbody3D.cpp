#include "pch.h"
#include "Rigidbody3D.h"

#include <cmath>
#include <span>

#include "../Diagnostics/Debug.h"
#include "GameObject.h"
#include "PropertyDescriptor.h"
#include "Rigidbody2D.h"

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>Rigidbody3D가 선언하는 속성이다. 실행 중 상태인 velocity는 장면에 저장하지 않는다.</summary>
    std::span<const PropertyDescriptor> Rigidbody3DProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<Rigidbody3D>(
                "velocity", "Velocity", &Rigidbody3D::GetVelocity, &Rigidbody3D::SetVelocity,
                PropertyTraits::NotSerialized),
            MakeProperty<Rigidbody3D>(
                "gravityScale", "Gravity Scale", &Rigidbody3D::GetGravityScale,
                &Rigidbody3D::SetGravityScale),
        };
        return properties;
    }
}

const ComponentType& Rigidbody3D::StaticType()
{
    static const ComponentType type{
        "Rigidbody3D", &Behaviour::StaticType(), &Rigidbody3DProperties,
        &MakeComponentInstance<Rigidbody3D> };
    return type;
}

void Rigidbody3D::OnAttached()
{
    GameObject* const owner = GetGameObject();
    if (!owner)
    {
        return;
    }
    if (owner->GetComponent<Rigidbody3D>() != this)
    {
        Diagnostics::Debug::LogWarning(
            "A GameObject has multiple Rigidbody3D components; only the first active one is simulated.");
    }
    if (owner->GetComponent<Rigidbody2D>())
    {
        Diagnostics::Debug::LogWarning(
            "A GameObject has both Rigidbody2D and Rigidbody3D components; neither is simulated while both are active.");
    }
}

void Rigidbody3D::SetVelocity(const Math::Vector3& velocity)
{
    mVelocity = {
        std::isfinite(velocity.GetX()) ? velocity.GetX() : 0.0f,
        std::isfinite(velocity.GetY()) ? velocity.GetY() : 0.0f,
        std::isfinite(velocity.GetZ()) ? velocity.GetZ() : 0.0f };
}

void Rigidbody3D::SetGravityScale(const float gravityScale)
{
    mGravityScale = std::isfinite(gravityScale) ? gravityScale : 1.0f;
}

void Rigidbody3D::AddContact(const Math::Vector3& normal)
{
    Rigidbody3DContact contact = Rigidbody3DContact::None;
    if (normal.GetX() > 0.0f)
    {
        contact = Rigidbody3DContact::Left;
    }
    else if (normal.GetX() < 0.0f)
    {
        contact = Rigidbody3DContact::Right;
    }
    else if (normal.GetY() > 0.0f)
    {
        contact = Rigidbody3DContact::Below;
    }
    else if (normal.GetY() < 0.0f)
    {
        contact = Rigidbody3DContact::Above;
    }
    else if (normal.GetZ() > 0.0f)
    {
        contact = Rigidbody3DContact::Backward;
    }
    else if (normal.GetZ() < 0.0f)
    {
        contact = Rigidbody3DContact::Forward;
    }
    mContacts = static_cast<Rigidbody3DContact>(
        static_cast<unsigned char>(mContacts) | static_cast<unsigned char>(contact));
}

}
