#pragma once

#include <optional>

#include "Collider2D.h"

namespace GameEngine::Runtime
{

class TilemapRenderer;

/// <summary>
/// 타일맵의 채워진 칸들을 그대로 콜라이더로 쓰는 컴포넌트다. 같은 오브젝트의
/// <see cref="TilemapRenderer"/>가 격자와 칸 크기를 정의하고, 비어 있지 않은 칸 하나하나가 그
/// 크기의 사각형이 된다.
///
/// 자기 모양 데이터를 갖지 않는 이유는 그것이 두 벌이 되기 때문이다. 타일을 칠하면 충돌도 함께
/// 바뀌어야 하는데, 사본을 두면 칠할 때마다 그것을 맞추는 코드가 필요하고 언젠가 한쪽만
/// 고쳐진다.
///
/// 넓은 판정은 격자 전체를 품는 사각형이고, 좁은 판정은 상대 사각형이 덮는 칸들만 본다 — 성긴
/// 타일맵이 자기 빈 자리로 충돌을 만들지 않는 이유가 그것이다.
///
/// 고체 sweep의 첫 범위는 회전하지 않은 타일맵이다. 콜라이더의 나머지 겹침 판정은 회전을 품는
/// 월드 AABB로 계속 답하지만, 셀 후보를 고르는 sweep까지 회전을 정확히 다루려면 별도의 도형
/// 모델이 필요하다.
/// </summary>
class TilemapCollider2D final : public Collider2D
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>
    /// 켜면 월드 위쪽에 노출된 칸의 윗면만 고체가 된다. 이동 시작 시 발바닥이 그 면 위에 있고
    /// 아래로 가로지를 때만 착지하며, 상승·측면 이동과 이미 아래에 있는 몸체는 통과한다.
    /// 기본값은 false로, 기존의 사각형 고체 판정을 유지한다. 음수 배율에서도 위는 월드 +Y다.
    /// OverlapsBox와 Enter/Exit는 타일 부피에 대한 기존 겹침 판정을 유지하며,
    /// isTrigger가 켜져 있으면 이 설정과 관계없이 물리적으로 막지 않는다.
    /// </summary>
    [[nodiscard]] bool IsOneWay() const { return mOneWay; }
    void SetOneWay(bool oneWay) { mOneWay = oneWay; }

    [[nodiscard]] Core::Aabb2D GetWorldBounds() const override;
    [[nodiscard]] bool OverlapsBox(const Core::Aabb2D& box) const override;
    [[nodiscard]] bool OverlapsCollider(const Collider2D& other) const override;

protected:
    /// <summary>채워진 칸 하나하나에 sweep을 물어 가장 이른 충돌만 돌려준다.</summary>
    [[nodiscard]] std::optional<Core::Aabb2DSweepHit> SweepBox(
        const Core::Aabb2D& movingBox, const Math::Vector2& worldDisplacement) const override;

private:
    /// <summary>격자를 정의하는 렌더러다. 같은 오브젝트에 없으면 null이고, 그때 이 콜라이더는 비어 있다.</summary>
    [[nodiscard]] const TilemapRenderer* FindTilemap() const;

    bool mOneWay = false;
};

}
