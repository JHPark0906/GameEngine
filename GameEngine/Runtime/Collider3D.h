#pragma once

#include <optional>
#include <vector>

#include "../Core/Aabb3D.h"
#include "Behaviour.h"

namespace GameEngine::Runtime
{

class Physics3DSystem;

/// <summary>움직이는 3D 콜라이더가 고체에 처음 닿는 자리다.</summary>
struct Collider3DSweepHit
{
    float fraction = 0.0f;
    Math::Vector3 normal;
};

/// <summary>
/// 겹침을 판정받는 3차원 영역이다. 어떤 모양인지는 파생이 답하고, 이 기반이 정하는 것은 월드
/// 공간의 축 정렬 경계 상자와 이번 프레임의 겹침 목록이다. 2D 콜라이더와 같은 오브젝트에 있어도
/// 서로의 물리 세계에는 참여하지 않는다.
///
/// 이 컴포넌트는 스스로 아무것도 밀지 않는다. 고체 충돌 해소는 <see cref="Physics3DSystem"/>과
/// <see cref="Rigidbody3D"/>의 책임이다. 회전한 도형도 회전한 모양 자체가 아니라, 그것을 품는
/// 월드 AABB로 판정한다.
/// </summary>
class Collider3D : public Behaviour
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();

    /// <summary>
    /// 지나갈 수 있는 영역인지다. 참이면 겹침은 알리되 Rigidbody3D의 고체 충돌 해소 대상에서는
    /// 빠진다.
    /// </summary>
    [[nodiscard]] bool IsTrigger() const { return mIsTrigger; }
    void SetTrigger(const bool isTrigger) { mIsTrigger = isTrigger; }

    /// <summary>월드 공간에서 이 콜라이더를 품는 축 정렬 상자다.</summary>
    [[nodiscard]] virtual Core::Aabb3D GetWorldBounds() const = 0;

    /// <summary>
    /// 월드 공간의 상자가 이 콜라이더의 실제 모양과 겹치는지다. 단순 상자는 넓은 판정과 실제
    /// 모양이 같으므로 기본이 참이다. 성긴 3D 도형이 생기면 이 질의를 재정의한다.
    /// </summary>
    /// <param name="box">이미 넓은 판정을 통과한 월드 상자다.</param>
    [[nodiscard]] virtual bool OverlapsBox(const Core::Aabb3D& box) const
    {
        static_cast<void>(box);
        return true;
    }

    /// <summary>지금 겹쳐 있는 콜라이더들의 인스턴스 id다. 시스템이 매 프레임 다시 채운다.</summary>
    [[nodiscard]] const std::vector<unsigned int>& GetOverlapping() const { return mOverlapping; }

    /// <summary>이번 프레임에 새로 겹치기 시작한 것들이다.</summary>
    [[nodiscard]] const std::vector<unsigned int>& GetEntered() const { return mEntered; }

    /// <summary>이번 프레임에 겹침이 끝난 것들이다.</summary>
    [[nodiscard]] const std::vector<unsigned int>& GetExited() const { return mExited; }

    /// <summary>그 콜라이더와 지금 겹쳐 있는지다.</summary>
    /// <param name="instanceId">상대 콜라이더의 인스턴스 id다.</param>
    [[nodiscard]] bool IsOverlapping(unsigned int instanceId) const;

protected:
    Collider3D() = default;

    /// <summary>
    /// 로컬 상자를 월드로 옮긴 뒤 다시 감싼 축 정렬 상자를 만든다. 여덟 모서리를 모두 변환하므로
    /// 회전·비균일 크기를 가진 부모 아래에서도 실제 자리를 빠뜨리지 않는다.
    /// </summary>
    [[nodiscard]] Core::Aabb3D TransformToWorld(const Core::Aabb3D& localBounds) const;

    /// <summary>
    /// 월드 공간의 움직이는 상자가 이 모양에 처음 닿는 때다. 물리 시스템만 쓰는 보호된 질의라,
    /// 나중에 복잡한 도형도 overlap과 고체 충돌에서 같은 모양을 답할 수 있다.
    /// </summary>
    [[nodiscard]] virtual std::optional<Collider3DSweepHit> SweepBox(
        const Core::Aabb3D& movingBox, const Math::Vector3& worldDisplacement) const;

private:
    friend class Physics3DSystem;

    /// <summary>이번 프레임의 겹침 목록을 받는다. 들어옴과 나감은 이전 목록과의 차이다.</summary>
    void ApplyOverlaps(std::vector<unsigned int> overlapping);

    /// <summary>프레임마다의 표시 — 들어옴, 나감 — 를 지운다.</summary>
    void ClearFrameFlags();

    bool mIsTrigger = false;
    std::vector<unsigned int> mOverlapping;
    std::vector<unsigned int> mEntered;
    std::vector<unsigned int> mExited;
};

}
