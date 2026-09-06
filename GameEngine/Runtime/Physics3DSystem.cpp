#include "pch.h"
#include "Physics3DSystem.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "BoxCollider3D.h"
#include "Collider3D.h"
#include "GameObject.h"
#include "Rigidbody2D.h"
#include "Rigidbody3D.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Transform.h"

namespace GameEngine::Runtime
{

namespace
{
    constexpr float MovementEpsilon = 0.000001f;

    /// <summary>고정 순회는 같은 장면 상태에서 같은 고체를 먼저 만나는 보수적인 기반이다.</summary>
    template <typename T>
    void SortByInstanceId(std::vector<T*>& objects)
    {
        std::ranges::sort(
            objects,
            [](const T* const left, const T* const right)
            {
                return left->GetInstanceId() < right->GetInstanceId();
            });
    }

    template <typename T>
    [[nodiscard]] bool HasActiveComponent(const GameObject& gameObject)
    {
        for (const T* const component : gameObject.GetComponents<T>())
        {
            if (component && component->IsActiveAndEnabled())
            {
                return true;
            }
        }
        return false;
    }
}

void Physics3DSystem::SetGravity(const Math::Vector3& gravity)
{
    mGravity = {
        std::isfinite(gravity.GetX()) ? gravity.GetX() : 0.0f,
        std::isfinite(gravity.GetY()) ? gravity.GetY() : 0.0f,
        std::isfinite(gravity.GetZ()) ? gravity.GetZ() : 0.0f };
}

void Physics3DSystem::CollectColliders(
    const Transform& transform, std::vector<Collider3D*>& colliders)
{
    if (GameObject* const gameObject = transform.GetGameObject())
    {
        if (!gameObject->IsActiveInHierarchy())
        {
            return;
        }
        for (Collider3D* const collider : gameObject->GetComponents<Collider3D>())
        {
            if (collider && collider->IsActiveAndEnabled())
            {
                colliders.push_back(collider);
            }
        }
    }
    for (const Transform* const child : transform.GetChildren())
    {
        if (child)
        {
            CollectColliders(*child, colliders);
        }
    }
}

void Physics3DSystem::CollectRigidbodies(
    const Transform& transform, std::vector<Rigidbody3D*>& rigidbodies)
{
    if (GameObject* const gameObject = transform.GetGameObject())
    {
        if (!gameObject->IsActiveInHierarchy())
        {
            return;
        }
        if (HasActiveComponent<Rigidbody2D>(*gameObject))
        {
            // 두 차원의 몸체가 Transform 하나를 함께 움직이면 시스템 호출 순서가 곧 결과가 된다.
            // 기존 2D나 새 3D 중 하나를 조용히 택하지 않고, 잘못된 조합을 모두 멈춘다.
            // 마지막으로 바닥에 닿았던 결과도 더는 현재 설정의 결과가 아니므로 함께 비운다. 이
            // 오브젝트만 건너뛰고, 자식에 붙은 별도 몸체는 아래 계층 순회에서 계속 찾는다.
            for (Rigidbody3D* const rigidbody : gameObject->GetComponents<Rigidbody3D>())
            {
                if (rigidbody && rigidbody->IsActiveAndEnabled())
                {
                    rigidbody->ClearContacts();
                }
            }
        }
        else
        {
            Rigidbody3D* selected = nullptr;
            for (Rigidbody3D* const rigidbody : gameObject->GetComponents<Rigidbody3D>())
            {
                if (rigidbody && rigidbody->IsActiveAndEnabled() &&
                    (!selected || rigidbody->GetInstanceId() < selected->GetInstanceId()))
                {
                    selected = rigidbody;
                }
            }
            if (selected)
            {
                rigidbodies.push_back(selected);
            }
        }
    }
    for (const Transform* const child : transform.GetChildren())
    {
        if (child)
        {
            CollectRigidbodies(*child, rigidbodies);
        }
    }
}

bool Physics3DSystem::AreOverlapping(
    const Collider3D& first, const Core::Aabb3D& firstBounds,
    const Collider3D& second, const Core::Aabb3D& secondBounds)
{
    return firstBounds.Overlaps(secondBounds) && first.OverlapsBox(secondBounds) &&
        second.OverlapsBox(firstBounds);
}

void Physics3DSystem::Simulate(SceneManager& sceneManager, const float deltaTime)
{
    const float maximumFrameTime = FixedDeltaTime * static_cast<float>(MaximumStepsPerFrame);
    if (std::isfinite(deltaTime) && deltaTime > 0.0f)
    {
        mAccumulator = (std::min)(mAccumulator + deltaTime, maximumFrameTime);
    }

    unsigned int steps = 0;
    while (mAccumulator + MovementEpsilon >= FixedDeltaTime && steps < MaximumStepsPerFrame)
    {
        SimulateFixedStep(sceneManager);
        mAccumulator = (std::max)(0.0f, mAccumulator - FixedDeltaTime);
        ++steps;
    }

    static_cast<void>(Synchronize(sceneManager));
}

void Physics3DSystem::SimulateFixedStep(SceneManager& sceneManager)
{
    std::vector<Collider3D*> colliders;
    std::vector<Rigidbody3D*> rigidbodies;
    for (const auto& [sceneId, scene] : sceneManager.GetActiveScenes())
    {
        if (!scene)
        {
            continue;
        }
        for (GameObject* const gameObject : scene->GetRootGameObjects())
        {
            if (!gameObject)
            {
                continue;
            }
            CollectColliders(gameObject->GetTransform(), colliders);
            CollectRigidbodies(gameObject->GetTransform(), rigidbodies);
        }
    }
    SortByInstanceId(colliders);
    SortByInstanceId(rigidbodies);

    for (Rigidbody3D* const rigidbody : rigidbodies)
    {
        rigidbody->ClearContacts();
        SimulateRigidbody(*rigidbody, colliders);
    }
}

void Physics3DSystem::SimulateRigidbody(
    Rigidbody3D& rigidbody, const std::vector<Collider3D*>& colliders)
{
    rigidbody.SetVelocity(
        rigidbody.GetVelocity() + mGravity * (rigidbody.GetGravityScale() * FixedDeltaTime));

    GameObject* const owner = rigidbody.GetGameObject();
    BoxCollider3D* movingCollider = nullptr;
    if (owner)
    {
        for (BoxCollider3D* const collider : owner->GetComponents<BoxCollider3D>())
        {
            if (collider && collider->IsActiveAndEnabled() && !collider->IsTrigger() &&
                (!movingCollider || collider->GetInstanceId() < movingCollider->GetInstanceId()))
            {
                movingCollider = collider;
            }
        }
    }

    const Math::Vector3 velocity = rigidbody.GetVelocity();
    MoveAlongAxis(
        rigidbody, movingCollider, colliders,
        { velocity.GetX() * FixedDeltaTime, 0.0f, 0.0f });
    MoveAlongAxis(
        rigidbody, movingCollider, colliders,
        { 0.0f, rigidbody.GetVelocity().GetY() * FixedDeltaTime, 0.0f });
    MoveAlongAxis(
        rigidbody, movingCollider, colliders,
        { 0.0f, 0.0f, rigidbody.GetVelocity().GetZ() * FixedDeltaTime });
}

std::optional<Collider3DSweepHit> Physics3DSystem::FindEarliestHit(
    const Rigidbody3D& rigidbody, const BoxCollider3D& movingCollider,
    const std::vector<Collider3D*>& colliders, const Math::Vector3& worldDisplacement) const
{
    if (std::abs(worldDisplacement.GetX()) <= MovementEpsilon &&
        std::abs(worldDisplacement.GetY()) <= MovementEpsilon &&
        std::abs(worldDisplacement.GetZ()) <= MovementEpsilon)
    {
        return std::nullopt;
    }

    const GameObject* const owner = rigidbody.GetGameObject();
    const Core::Aabb3D movingBounds = movingCollider.GetWorldBounds();
    if (!owner || !movingBounds.HasVolume())
    {
        return std::nullopt;
    }

    std::optional<Collider3DSweepHit> earliest;
    for (Collider3D* const target : colliders)
    {
        if (!target || target->IsTrigger())
        {
            continue;
        }
        const GameObject* const targetOwner = target->GetGameObject();
        if (!targetOwner || targetOwner == owner)
        {
            continue;
        }

        // 이 첫 단계는 정적 지형만 해소한다. 어느 차원이든 활성 몸체가 소유한 콜라이더를 벽으로
        // 쓰면 두 물리계의 실행 순서가 결과가 된다. 질량·반발 모델을 설계할 때까지 모두 지난다.
        if (HasActiveComponent<Rigidbody2D>(*targetOwner) ||
            HasActiveComponent<Rigidbody3D>(*targetOwner))
        {
            continue;
        }

        const std::optional<Collider3DSweepHit> hit =
            target->SweepBox(movingBounds, worldDisplacement);
        if (hit && (!earliest || hit->fraction < earliest->fraction))
        {
            earliest = hit;
        }
    }
    return earliest;
}

void Physics3DSystem::MoveAlongAxis(
    Rigidbody3D& rigidbody, const BoxCollider3D* const movingCollider,
    const std::vector<Collider3D*>& colliders, const Math::Vector3& worldDisplacement) const
{
    if (std::abs(worldDisplacement.GetX()) <= MovementEpsilon &&
        std::abs(worldDisplacement.GetY()) <= MovementEpsilon &&
        std::abs(worldDisplacement.GetZ()) <= MovementEpsilon)
    {
        return;
    }

    Transform* const transform = rigidbody.GetTransform();
    if (!transform)
    {
        return;
    }

    float fraction = 1.0f;
    std::optional<Collider3DSweepHit> hit;
    if (movingCollider)
    {
        hit = FindEarliestHit(rigidbody, *movingCollider, colliders, worldDisplacement);
        if (hit)
        {
            fraction = (std::clamp)(hit->fraction, 0.0f, 1.0f);
        }
    }

    const Math::Vector3 current = transform->GetWorldPosition();
    const Math::Vector3 destination{
        current.GetX() + worldDisplacement.GetX() * fraction,
        current.GetY() + worldDisplacement.GetY() * fraction,
        current.GetZ() + worldDisplacement.GetZ() * fraction };
    if (!transform->SetWorldPosition(destination))
    {
        return;
    }

    if (!hit)
    {
        return;
    }

    rigidbody.AddContact(hit->normal);
    Math::Vector3 velocity = rigidbody.GetVelocity();
    if (hit->normal.GetX() != 0.0f)
    {
        velocity = { 0.0f, velocity.GetY(), velocity.GetZ() };
    }
    else if (hit->normal.GetY() != 0.0f)
    {
        velocity = { velocity.GetX(), 0.0f, velocity.GetZ() };
    }
    else if (hit->normal.GetZ() != 0.0f)
    {
        velocity = { velocity.GetX(), velocity.GetY(), 0.0f };
    }
    rigidbody.SetVelocity(velocity);
}

std::size_t Physics3DSystem::Synchronize(SceneManager& sceneManager)
{
    std::vector<Collider3D*> colliders;
    for (const auto& [sceneId, scene] : sceneManager.GetActiveScenes())
    {
        if (!scene)
        {
            continue;
        }
        for (GameObject* const gameObject : scene->GetRootGameObjects())
        {
            if (gameObject)
            {
                CollectColliders(gameObject->GetTransform(), colliders);
            }
        }
    }
    SortByInstanceId(colliders);

    std::vector<Core::Aabb3D> bounds;
    bounds.reserve(colliders.size());
    for (const Collider3D* const collider : colliders)
    {
        bounds.push_back(collider->GetWorldBounds());
    }

    std::vector<std::vector<unsigned int>> overlaps(colliders.size());
    std::size_t overlappingPairs = 0;
    for (std::size_t first = 0; first < colliders.size(); ++first)
    {
        for (std::size_t second = first + 1; second < colliders.size(); ++second)
        {
            if (!AreOverlapping(
                    *colliders[first], bounds[first], *colliders[second], bounds[second]))
            {
                continue;
            }
            overlaps[first].push_back(colliders[second]->GetInstanceId());
            overlaps[second].push_back(colliders[first]->GetInstanceId());
            ++overlappingPairs;
        }
    }

    for (std::size_t index = 0; index < colliders.size(); ++index)
    {
        colliders[index]->ClearFrameFlags();
        colliders[index]->ApplyOverlaps(std::move(overlaps[index]));
    }
    return overlappingPairs;
}

}
