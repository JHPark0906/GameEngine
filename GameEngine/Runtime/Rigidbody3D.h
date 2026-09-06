#pragma once

#include "../Math/Vector.h"
#include "Behaviour.h"

namespace GameEngine::Runtime
{

class Physics3DSystem;

/// <summary>고체와 맞닿은 3D 몸체의 면이다. 여러 면에 동시에 닿을 수 있다.</summary>
enum class Rigidbody3DContact : unsigned char
{
    None = 0,
    Left = 1 << 0,
    Right = 1 << 1,
    Above = 1 << 2,
    Below = 1 << 3,
    Forward = 1 << 4,
    Backward = 1 << 5,
};

/// <summary>
/// 3D 물리에서 속도와 중력의 영향을 받는 몸체다. 모양은 같은 GameObject의
/// <see cref="BoxCollider3D"/>가 맡고, 장면 전체의 충돌 판정·해소는
/// <see cref="Physics3DSystem"/>이 맡는다. 질량, 반발, 마찰, 회전·각속도는 첫 범위 밖이다.
///
/// 활성 Rigidbody3D와 BoxCollider3D는 GameObject마다 하나씩만 지원한다. 활성 3D 몸체끼리는
/// 아직 서로 밀지 않는다. Rigidbody2D와 동시에 활성화하면 둘 다 같은 Transform을 적분하게 되므로,
/// 양쪽 물리 시스템은 그 잘못된 설정의 두 몸체를 모두 건너뛴다.
/// </summary>
class Rigidbody3D final : public Behaviour
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>초당 월드 단위의 현재 속도다. 물리 해소가 막힌 축 성분을 0으로 만든다.</summary>
    [[nodiscard]] const Math::Vector3& GetVelocity() const { return mVelocity; }
    void SetVelocity(const Math::Vector3& velocity);

    /// <summary>전역 중력에 곱하는 계수다. 0이면 중력을 받지 않는다.</summary>
    [[nodiscard]] float GetGravityScale() const { return mGravityScale; }
    void SetGravityScale(float gravityScale);

    /// <summary>가장 최근 물리 스텝에서 닿은 고체 면들이다. 저장하지 않는 물리 결과다.</summary>
    [[nodiscard]] Rigidbody3DContact GetContacts() const { return mContacts; }

    /// <summary>지정한 면에 닿아 있는지다.</summary>
    [[nodiscard]] bool HasContact(const Rigidbody3DContact contact) const
    {
        return (static_cast<unsigned char>(mContacts) & static_cast<unsigned char>(contact)) != 0;
    }

    /// <summary>아래쪽 고체에 닿아 있는지다. 점프 같은 게임 로직이 읽는 편의 질의다.</summary>
    [[nodiscard]] bool IsGrounded() const { return HasContact(Rigidbody3DContact::Below); }

private:
    friend class Physics3DSystem;

    /// <summary>같은 오브젝트에 중복 또는 다른 차원의 몸체가 붙은 설정을 한 번 진단한다.</summary>
    void OnAttached() override;

    /// <summary>이번 fixed step의 접촉을 비운다.</summary>
    void ClearContacts() { mContacts = Rigidbody3DContact::None; }

    /// <summary>충돌 법선이 가리키는 반대편 몸체 면을 접촉으로 기록한다.</summary>
    void AddContact(const Math::Vector3& normal);

    Math::Vector3 mVelocity;
    float mGravityScale = 1.0f;
    Rigidbody3DContact mContacts = Rigidbody3DContact::None;
};

}
