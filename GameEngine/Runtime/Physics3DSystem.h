#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "../Math/Vector.h"
#include "Collider3D.h"

namespace GameEngine::Runtime
{

class BoxCollider3D;
class Rigidbody3D;
class SceneManager;
class Transform;

/// <summary>
/// 활성 장면의 <see cref="Collider3D"/>들이 서로 겹치는지 판정하고, Rigidbody3D의 중력·정적
/// 고체 충돌을 고정 스텝으로 해소하는 시스템이다. 2D 물리와는 별도 세계라 서로 다른 차원의
/// 콜라이더는 만나지 않는다.
///
/// 겹침 상태는 프레임 마지막 위치에서 한 번만 갱신한다. 이로써 Trigger의 Enter/Exit가 한 렌더
/// 프레임 안의 중간 물리 위치가 아니라 스크립트와 렌더링이 읽는 최종 Transform을 나타낸다.
/// 같은 GameObject의 활성 Rigidbody2D가 소유한 Collider3D는 겹침에는 남지만, 3D 고체 충돌에서는
/// 정적 벽으로 취급하지 않는다. 두 물리 세계의 호출 순서가 충돌 결과가 되는 것을 막는 제한이다.
/// </summary>
class Physics3DSystem final
{
public:
    /// <summary>기본 중력이다. +Y가 위인 3D 장면에서 아래 방향으로 작용한다.</summary>
    [[nodiscard]] const Math::Vector3& GetGravity() const { return mGravity; }
    void SetGravity(const Math::Vector3& gravity);

    /// <summary>내부 물리 스텝의 길이다. Behaviour::Update와는 독립적으로 고정된다.</summary>
    [[nodiscard]] static constexpr float GetFixedDeltaTime() { return FixedDeltaTime; }

    /// <summary>
    /// 프레임 시간을 고정 스텝으로 나눠 Rigidbody3D를 적분하고, 마지막 위치에서 겹침 상태를
    /// 갱신한다. 긴 프레임은 정한 수만큼만 따라가 누적 지연을 막는다.
    /// </summary>
    /// <param name="sceneManager">활성 장면들을 쥔 매니저다.</param>
    /// <param name="deltaTime">이번 렌더 프레임에 흐른 초 단위 시간이다.</param>
    void Simulate(SceneManager& sceneManager, float deltaTime);

    /// <summary>활성 장면의 3D 콜라이더 겹침을 판정하고 결과를 나눠 준다.</summary>
    /// <param name="sceneManager">활성 장면들을 쥔 매니저다.</param>
    /// <returns>이번 프레임에 겹친 짝의 수다. 진단과 시험이 읽는다.</returns>
    std::size_t Synchronize(SceneManager& sceneManager);

private:
    static constexpr float FixedDeltaTime = 1.0f / 60.0f;
    static constexpr unsigned int MaximumStepsPerFrame = 8;

    /// <summary>계층을 훑으며 판정에 참여할 콜라이더를 모은다.</summary>
    static void CollectColliders(const Transform& transform, std::vector<Collider3D*>& colliders);

    /// <summary>계층을 훑으며 fixed step을 받을 3D 몸체를 모은다.</summary>
    static void CollectRigidbodies(
        const Transform& transform, std::vector<Rigidbody3D*>& rigidbodies);

    /// <summary>넓은 판정과 양쪽 도형의 좁은 판정을 한 규칙으로 묶는다.</summary>
    [[nodiscard]] static bool AreOverlapping(
        const Collider3D& first, const Core::Aabb3D& firstBounds,
        const Collider3D& second, const Core::Aabb3D& secondBounds);

    /// <summary>이번 fixed step의 모든 몸체를 중력·충돌 순으로 움직인다.</summary>
    void SimulateFixedStep(SceneManager& sceneManager);

    /// <summary>한 몸체의 X, Y, Z 이동을 따로 해결해 고체 면을 따라 미끄러지게 한다.</summary>
    void SimulateRigidbody(
        Rigidbody3D& rigidbody, const std::vector<Collider3D*>& colliders);

    /// <summary>한 축 이동의 가장 이른 정적 고체 충돌이다.</summary>
    [[nodiscard]] std::optional<Collider3DSweepHit> FindEarliestHit(
        const Rigidbody3D& rigidbody, const BoxCollider3D& movingCollider,
        const std::vector<Collider3D*>& colliders,
        const Math::Vector3& worldDisplacement) const;

    /// <summary>충돌 전까지 움직이고, 막힌 축의 속도와 접촉을 갱신한다.</summary>
    void MoveAlongAxis(
        Rigidbody3D& rigidbody, const BoxCollider3D* movingCollider,
        const std::vector<Collider3D*>& colliders,
        const Math::Vector3& worldDisplacement) const;

    Math::Vector3 mGravity{ 0.0f, -9.81f, 0.0f };
    float mAccumulator = 0.0f;
};

}
