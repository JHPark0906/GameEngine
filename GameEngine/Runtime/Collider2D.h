#pragma once

#include <optional>
#include <vector>

#include "../Math/Aabb2D.h"
#include "Behaviour.h"

namespace GameEngine::Runtime
{

class Physics2DSystem;

/// <summary>
/// 겹침을 판정받는 2차원 영역이다. 어떤 모양인지는 파생이 답하고, 이 기반이 정하는 것은 두
/// 가지다: 월드 공간에서 자기를 품는 축 정렬 사각형과, 이번 프레임에 누구와 겹쳐 있는지.
///
/// 겹침을 스스로 정하지 않고 <see cref="Physics2DSystem"/>에게서 받는 이유는
/// <see cref="Button"/>이 눌림을 스스로 정하지 않는 이유와 같다: "누가 누구와 겹치는가"는 한
/// 컴포넌트가 혼자 답할 수 없는 질문이고, 장면 전체를 보는 쪽만 답할 수 있다.
///
/// 이 컴포넌트는 스스로 아무것도 밀지 않는다. 겹쳤다고 말하고, 물리 시스템이 필요할 때
/// 움직이는 사각형의 충돌 시점만 물을 수 있게 한다. 반발·마찰·위치 보정의 정책은 여기 아닌
/// <see cref="Physics2DSystem"/>과 <see cref="Rigidbody2D"/>에 있다.
///
/// 판정은 월드 공간에서 축에 나란하다. 회전한 오브젝트의 콜라이더는 회전한 모양이 아니라 그것을
/// 품는 사각형으로 판정된다.
/// </summary>
class Collider2D : public Behaviour
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();

    /// <summary>
    /// 지나갈 수 있는 영역인지다. 참이면 겹침을 알리기만 하는 감지기로 쓴다는 뜻이다. overlap
    /// 판정은 그대로 참여하지만, Rigidbody2D의 고체 충돌 해소 대상에서는 빠진다.
    /// </summary>
    [[nodiscard]] bool IsTrigger() const { return mIsTrigger; }
    void SetTrigger(const bool isTrigger) { mIsTrigger = isTrigger; }

    /// <summary>
    /// 월드 공간에서 이 콜라이더를 품는 축 정렬 사각형이다. 넓은 판정이 이것으로 먼저 걸러진다.
    /// </summary>
    [[nodiscard]] virtual Math::Aabb2D GetWorldBounds() const = 0;

    /// <summary>
    /// 월드 공간의 사각형이 이 콜라이더의 실제 모양과 겹치는지다. 기본은 참인데, 이 사각형은
    /// 이미 <see cref="GetWorldBounds"/>를 통과한 것이고 단순한 모양에서는 그 둘이 같기
    /// 때문이다. 품는 사각형보다 성긴 모양 — 타일맵 — 이 이것을 재정의한다.
    /// </summary>
    /// <param name="box">이미 넓은 판정을 통과한 월드 사각형이다.</param>
    [[nodiscard]] virtual bool OverlapsBox(const Math::Aabb2D& box) const
    {
        static_cast<void>(box);
        return true;
    }

    /// <summary>
    /// 넓은 판정을 통과한 상대 도형과 겹치는지다. 성긴 도형은 자기 부분 도형들을 상대에게
    /// 물어야 한다. 서로의 전체 경계에 점유한 칸이 있다는 것만으로 두 타일맵이 닿지는 않는다.
    /// </summary>
    [[nodiscard]] virtual bool OverlapsCollider(const Collider2D& other) const
    {
        return OverlapsBox(other.GetWorldBounds());
    }

    /// <summary>지금 겹쳐 있는 콜라이더들의 인스턴스 id다. 시스템이 매 프레임 다시 채운다.</summary>
    [[nodiscard]] const std::vector<unsigned int>& GetOverlapping() const { return mOverlapping; }

    /// <summary>이번 프레임에 새로 겹치기 시작한 것들이다. 다음 프레임에는 비어 있다.</summary>
    [[nodiscard]] const std::vector<unsigned int>& GetEntered() const { return mEntered; }

    /// <summary>이번 프레임에 겹침이 끝난 것들이다. 다음 프레임에는 비어 있다.</summary>
    [[nodiscard]] const std::vector<unsigned int>& GetExited() const { return mExited; }

    /// <summary>그 콜라이더와 지금 겹쳐 있는지다.</summary>
    /// <param name="instanceId">상대 콜라이더의 인스턴스 id다.</param>
    [[nodiscard]] bool IsOverlapping(unsigned int instanceId) const;

protected:
    Collider2D() = default;

    /// <summary>
    /// 이 콜라이더가 붙은 오브젝트의 로컬 사각형을 월드로 옮겨 품는 사각형을 만든다. 네 모서리를
    /// 모두 변환하므로 회전한 부모 아래에서도 실제 자리를 품는다.
    /// </summary>
    /// <param name="localBounds">오브젝트 로컬 공간의 사각형이다.</param>
    [[nodiscard]] Math::Aabb2D TransformToWorld(const Math::Aabb2D& localBounds) const;

    /// <summary>월드 사각형을 이 오브젝트의 로컬 공간으로 되돌린다. 회전이 없을 때 정확하다.</summary>
    /// <param name="worldBox">월드 공간의 사각형이다.</param>
    [[nodiscard]] Math::Aabb2D TransformToLocal(const Math::Aabb2D& worldBox) const;

    /// <summary>
    /// 월드 공간의 움직이는 사각형이 이 모양에 처음 닿는 때다. 단순한 모양은 자신의 품는
    /// 사각형으로 답하고, 타일맵처럼 빈 부분이 있는 모양은 채워진 부분만 재정의한다.
    ///
    /// 물리 시스템만 쓰는 보호된 질의다. 게임 코드가 같은 sweep을 별도로 구현하면 겹침 판정과
    /// 고체 충돌이 서로 다른 모양을 보게 되므로, 도형의 정의를 가진 이 계층에 둔다.
    /// </summary>
    /// <param name="movingBox">현재 월드 공간의 움직이는 사각형이다.</param>
    /// <param name="worldDisplacement">이번 스텝의 월드 이동 거리다.</param>
    /// <returns>최초 충돌, 또는 길이 비어 있으면 nullopt다.</returns>
    [[nodiscard]] virtual std::optional<Math::Aabb2DSweepHit> SweepBox(
        const Math::Aabb2D& movingBox, const Math::Vector2& worldDisplacement) const;

private:
    // 겹침을 채우는 것은 시스템 하나다. 다른 곳이 이것을 넣을 수 있으면 화면이 보이는 상태와
    // 스크립트가 읽는 상태가 어긋난다.
    friend class Physics2DSystem;

    /// <summary>이번 프레임의 겹침 목록을 받는다. 들어옴과 나감은 이전 목록과의 차이다.</summary>
    void ApplyOverlaps(std::vector<unsigned int> overlapping);

    /// <summary>프레임마다의 표시 — 들어옴, 나감 — 를 지운다. 시스템이 프레임 앞에서 부른다.</summary>
    void ClearFrameFlags();

    bool mIsTrigger = false;
    std::vector<unsigned int> mOverlapping;
    std::vector<unsigned int> mEntered;
    std::vector<unsigned int> mExited;
};

}
