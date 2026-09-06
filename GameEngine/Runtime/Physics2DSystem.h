#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "../Core/Aabb2D.h"
#include "../Math/Vector.h"

namespace GameEngine::Runtime
{

class BoxCollider2D;
class Collider2D;
class Rigidbody2D;
class SceneManager;
class Transform;

/// <summary>
/// 활성 장면의 <see cref="Collider2D"/>들이 서로 겹치는지 판정해, 들어옴·머묾·나감을 각
/// 콜라이더에 나눠 주는 시스템이다.
///
/// 이것이 따로 있는 이유는 겹침이 짝의 성질이기 때문이다. 콜라이더 하나는 자기 모양만 알고,
/// "누가 나와 겹치는가"는 장면의 콜라이더를 모두 보는 쪽만 답할 수 있다 —
/// <see cref="UIEventSystem"/>이 커서의 주인을 정하는 것과 같은 자리이며, 같은 이유로 프레임의
/// 상태가 확정된 뒤에 한 번 돈다.
///
/// 겹침 상태는 <see cref="Synchronize"/>가 프레임 마지막 위치에서 한 번만 갱신한다.
/// Rigidbody2D가 붙은 BoxCollider2D의 중력·고체 충돌은 <see cref="Simulate"/>가 그보다 먼저
/// 고정 스텝으로 처리한다. 둘을 분리해 trigger의 Enter/Exit가 한 렌더 프레임 안의 중간 물리
/// 위치를 보지 않게 한다.
/// 같은 GameObject의 활성 Rigidbody3D가 소유한 Collider2D는 겹침에는 남지만, 2D 고체 충돌에서는
/// 정적 벽으로 취급하지 않는다. 두 물리 세계의 호출 순서가 충돌 결과가 되는 것을 막는 제한이다.
/// </summary>
class Physics2DSystem final
{
public:
    /// <summary>기본 중력이다. +Y가 위인 2D 장면에서 아래 방향으로 작용한다.</summary>
    [[nodiscard]] const Math::Vector2& GetGravity() const { return mGravity; }
    void SetGravity(const Math::Vector2& gravity);

    /// <summary>내부 물리 스텝의 길이다. Behaviour::Update와는 독립적으로 고정된다.</summary>
    [[nodiscard]] static constexpr float GetFixedDeltaTime() { return FixedDeltaTime; }

    /// <summary>
    /// 프레임 시간을 고정 스텝으로 나눠 Rigidbody2D를 적분하고, 마지막 위치에서 기존 overlap
    /// 상태를 갱신한다. 긴 프레임은 최대 스텝 수까지만 따라가서 누적 지연이 다시 지연을 만드는
    /// 상황을 막는다.
    /// </summary>
    /// <param name="sceneManager">활성 장면들을 쥔 매니저다.</param>
    /// <param name="deltaTime">이번 렌더 프레임에 흐른 초 단위 시간이다.</param>
    void Simulate(SceneManager& sceneManager, float deltaTime);

    /// <summary>
    /// 활성 장면의 콜라이더들을 모아 겹침을 판정하고 그 결과를 나눠 준다.
    ///
    /// 판정은 두 단계다. 먼저 각자를 품는 사각형끼리 걸러 내고, 통과한 짝만 서로에게 실제 모양을
    /// 묻는다 — 성긴 타일맵이 자기 빈 자리로 충돌을 만들지 않는 자리가 그 두 번째 단계다.
    /// </summary>
    /// <param name="sceneManager">활성 장면들을 쥔 매니저다.</param>
    /// <returns>이번 프레임에 겹친 짝의 수다. 진단과 시험이 읽는다.</returns>
    std::size_t Synchronize(SceneManager& sceneManager);

private:
    static constexpr float FixedDeltaTime = 1.0f / 60.0f;
    static constexpr unsigned int MaximumStepsPerFrame = 8;

    /// <summary>계층을 훑으며 판정에 참여할 콜라이더를 모은다.</summary>
    static void CollectColliders(const Transform& transform, std::vector<Collider2D*>& colliders);

    /// <summary>계층을 훑으며 fixed step을 받는 몸체를 모은다.</summary>
    static void CollectRigidbodies(
        const Transform& transform, std::vector<Rigidbody2D*>& rigidbodies);

    /// <summary>넓은 판정과 양쪽 도형의 좁은 판정을 한 규칙으로 묶는다.</summary>
    [[nodiscard]] static bool AreOverlapping(
        const Collider2D& first, const Core::Aabb2D& firstBounds,
        const Collider2D& second, const Core::Aabb2D& secondBounds);

    /// <summary>이번 fixed step의 모든 몸체를 중력·충돌 순으로 움직인다.</summary>
    void SimulateFixedStep(SceneManager& sceneManager);

    /// <summary>한 몸체의 X와 Y 이동을 따로 해결해 벽을 따라 미끄러지게 한다.</summary>
    void SimulateRigidbody(
        Rigidbody2D& rigidbody, const std::vector<Collider2D*>& colliders);

    /// <summary>한 축 이동의 가장 이른 정적 고체 충돌이다.</summary>
    [[nodiscard]] std::optional<Core::Aabb2DSweepHit> FindEarliestHit(
        const Rigidbody2D& rigidbody, const BoxCollider2D& movingCollider,
        const std::vector<Collider2D*>& colliders,
        const Math::Vector2& worldDisplacement) const;

    /// <summary>충돌 전까지 움직이고, 막힌 축의 속도와 접촉을 갱신한다.</summary>
    void MoveAlongAxis(
        Rigidbody2D& rigidbody, const BoxCollider2D* movingCollider,
        const std::vector<Collider2D*>& colliders,
        const Math::Vector2& worldDisplacement) const;

    Math::Vector2 mGravity{ 0.0f, -9.81f };
    float mAccumulator = 0.0f;
};

}
