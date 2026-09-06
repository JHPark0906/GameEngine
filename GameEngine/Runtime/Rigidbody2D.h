#pragma once

#include "../Math/Vector.h"
#include "Behaviour.h"

namespace GameEngine::Runtime
{

class Physics2DSystem;

/// <summary>고체와 맞닿은 몸체의 면이다. 여러 면에 동시에 닿을 수 있다.</summary>
enum class Rigidbody2DContact : unsigned char
{
    None = 0,
    Left = 1 << 0,
    Right = 1 << 1,
    Above = 1 << 2,
    Below = 1 << 3,
};

/// <summary>
/// 2D 물리에서 속도와 중력의 영향을 받는 몸체다. 모양은 같은 GameObject의 기존
/// <see cref="BoxCollider2D"/>가 맡고, 장면 전체의 충돌 판정·해소는
/// <see cref="Physics2DSystem"/>이 맡는다. 따라서 이 컴포넌트는 별도의 충돌 도형이나
/// 월드를 갖지 않는다.
///
/// 첫 범위에서는 GameObject마다 활성 Rigidbody2D와 BoxCollider2D를 하나씩만 지원한다. 같은
/// 오브젝트에 활성 Rigidbody2D가 여럿이면 가장 먼저 만들어진 하나만 물리 스텝을 받고, 추가된
/// 것은 진단으로 알린다. 활성 Rigidbody2D끼리는 서로 밀지 않는다. 움직이는 플레이어가 타일맵과
/// 일반 정적 Collider2D에 막히는 기반이며, body-body 반발·질량·마찰은 접촉 모델을 넓힐 때
/// 따로 설계한다.
/// </summary>
class Rigidbody2D final : public Behaviour
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>초당 월드 단위의 현재 속도다. 물리 해소가 막힌 축 성분을 0으로 만든다.</summary>
    [[nodiscard]] const Math::Vector2& GetVelocity() const { return mVelocity; }
    void SetVelocity(const Math::Vector2& velocity);

    /// <summary>
    /// 속도와 마지막 접촉을 함께 비운다. 순간 이동이나 재시작 직후 이전 위치의 grounded 결과를
    /// 사용하지 않도록 호출한다. 위치는 바꾸지 않으며 새 접촉은 다음 물리 스텝이 계산한다.
    /// </summary>
    void ResetMotion();

    /// <summary>전역 중력에 곱하는 계수다. 0이면 중력을 받지 않는다.</summary>
    [[nodiscard]] float GetGravityScale() const { return mGravityScale; }
    void SetGravityScale(float gravityScale);

    /// <summary>가장 최근 물리 스텝에서 닿은 고체 면들이다. 저장하지 않는 물리 결과다.</summary>
    [[nodiscard]] Rigidbody2DContact GetContacts() const { return mContacts; }

    /// <summary>지정한 면에 닿아 있는지다.</summary>
    [[nodiscard]] bool HasContact(const Rigidbody2DContact contact) const
    {
        return (static_cast<unsigned char>(mContacts) & static_cast<unsigned char>(contact)) != 0;
    }

    /// <summary>아래쪽 고체에 닿아 있는지다. 점프 같은 게임 로직이 읽는 편의 질의다.</summary>
    [[nodiscard]] bool IsGrounded() const { return HasContact(Rigidbody2DContact::Below); }

private:
    friend class Physics2DSystem;

    /// <summary>같은 오브젝트에 두 번째 몸체가 붙은 설정을 한 번 진단한다.</summary>
    void OnAttached() override;

    /// <summary>이번 fixed step의 접촉을 비운다.</summary>
    void ClearContacts() { mContacts = Rigidbody2DContact::None; }

    /// <summary>충돌 법선이 가리키는 반대편 몸체 면을 접촉으로 기록한다.</summary>
    void AddContact(const Math::Vector2& normal);

    Math::Vector2 mVelocity;
    float mGravityScale = 1.0f;
    Rigidbody2DContact mContacts = Rigidbody2DContact::None;
};

}
