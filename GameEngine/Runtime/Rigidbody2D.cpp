#include "pch.h"
#include "Rigidbody2D.h"

#include <cmath>
#include <span>

#include "GameObject.h"
#include "PropertyDescriptor.h"
#include "Rigidbody3D.h"
#include "../Diagnostics/Debug.h"

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// velocity는 장면에 적는 초기 설정이 아니라 실행 중 적분되는 상태다. 파일에 남기면 저장한
    /// 순간의 낙하 속도로 다음 실행을 시작하게 되므로, 인스펙터에는 보여도 직렬화에서는 뺀다.
    /// </summary>
    std::span<const PropertyDescriptor> Rigidbody2DProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<Rigidbody2D>(
                "velocity", "Velocity", &Rigidbody2D::GetVelocity, &Rigidbody2D::SetVelocity,
                PropertyTraits::NotSerialized),
            MakeProperty<Rigidbody2D>(
                "gravityScale", "Gravity Scale", &Rigidbody2D::GetGravityScale,
                &Rigidbody2D::SetGravityScale),
        };
        return properties;
    }
}

const ComponentType& Rigidbody2D::StaticType()
{
    static const ComponentType type{
        "Rigidbody2D", &Behaviour::StaticType(), &Rigidbody2DProperties,
        &MakeComponentInstance<Rigidbody2D> };
    return type;
}

void Rigidbody2D::OnAttached()
{
    GameObject* const owner = GetGameObject();
    if (owner && owner->GetComponent<Rigidbody2D>() != this)
    {
        // 한 Transform을 둘이 적분하면 속도가 컴포넌트 수만큼 곱해진다. 지금은 복합 몸체의
        // 질량·도형 규칙이 없으므로, 첫 몸체만 쓰는 제한을 붙이는 순간에 알려 준다.
        Diagnostics::Debug::LogWarning(
            "A GameObject has multiple Rigidbody2D components; only the first active one is simulated.");
    }
    if (owner && owner->GetComponent<Rigidbody3D>())
    {
        Diagnostics::Debug::LogWarning(
            "A GameObject has both Rigidbody2D and Rigidbody3D components; neither is simulated while both are active.");
    }
}

void Rigidbody2D::SetVelocity(const Math::Vector2& velocity)
{
    // 잘못된 입력 하나가 Transform까지 NaN으로 만들면 장면의 모든 AABB가 무너진다. 유효한 축은
    // 그대로 두어, 한 축을 잘못 적었다고 다른 축의 입력까지 잃지는 않는다.
    mVelocity = {
        std::isfinite(velocity.GetX()) ? velocity.GetX() : 0.0f,
        std::isfinite(velocity.GetY()) ? velocity.GetY() : 0.0f };
}

void Rigidbody2D::SetGravityScale(const float gravityScale)
{
    mGravityScale = std::isfinite(gravityScale) ? gravityScale : 1.0f;
}

void Rigidbody2D::ResetMotion()
{
    mVelocity = {};
    ClearContacts();
}

void Rigidbody2D::AddContact(const Math::Vector2& normal)
{
    Rigidbody2DContact contact = Rigidbody2DContact::None;
    if (normal.GetX() > 0.0f)
    {
        contact = Rigidbody2DContact::Left;
    }
    else if (normal.GetX() < 0.0f)
    {
        contact = Rigidbody2DContact::Right;
    }
    else if (normal.GetY() > 0.0f)
    {
        contact = Rigidbody2DContact::Below;
    }
    else if (normal.GetY() < 0.0f)
    {
        contact = Rigidbody2DContact::Above;
    }
    mContacts = static_cast<Rigidbody2DContact>(
        static_cast<unsigned char>(mContacts) | static_cast<unsigned char>(contact));
}

}
