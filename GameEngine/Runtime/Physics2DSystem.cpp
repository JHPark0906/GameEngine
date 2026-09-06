#include "pch.h"
#include "Physics2DSystem.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "BoxCollider2D.h"
#include "Collider2D.h"
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

void Physics2DSystem::SetGravity(const Math::Vector2& gravity)
{
    mGravity = {
        std::isfinite(gravity.GetX()) ? gravity.GetX() : 0.0f,
        std::isfinite(gravity.GetY()) ? gravity.GetY() : 0.0f };
}

void Physics2DSystem::CollectColliders(
    const Transform& transform, std::vector<Collider2D*>& colliders)
{
    if (GameObject* const gameObject = transform.GetGameObject())
    {
        if (!gameObject->IsActiveInHierarchy())
        {
            // 꺼진 가지는 통째로 빠진다. 보이지 않는 것이 무언가와 겹칠 수는 없다.
            return;
        }
        for (Collider2D* const collider : gameObject->GetComponents<Collider2D>())
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

void Physics2DSystem::CollectRigidbodies(
    const Transform& transform, std::vector<Rigidbody2D*>& rigidbodies)
{
    if (GameObject* const gameObject = transform.GetGameObject())
    {
        if (!gameObject->IsActiveInHierarchy())
        {
            return;
        }
        if (HasActiveComponent<Rigidbody3D>(*gameObject))
        {
            // 두 차원의 몸체가 Transform 하나를 함께 움직이면 시스템 호출 순서가 곧 결과가 된다.
            // 기존 2D나 새 3D 중 하나를 조용히 택하지 않고, 잘못된 조합을 모두 멈춘다.
            // 마지막으로 바닥에 닿았던 결과도 더는 현재 설정의 결과가 아니므로 함께 비운다. 이
            // 오브젝트만 건너뛰고, 자식에 붙은 별도 몸체는 아래 계층 순회에서 계속 찾는다.
            for (Rigidbody2D* const rigidbody : gameObject->GetComponents<Rigidbody2D>())
            {
                if (rigidbody && rigidbody->IsActiveAndEnabled())
                {
                    rigidbody->ClearContacts();
                }
            }
        }
        else
        {
            Rigidbody2D* selected = nullptr;
            for (Rigidbody2D* const rigidbody : gameObject->GetComponents<Rigidbody2D>())
            {
                if (rigidbody && rigidbody->IsActiveAndEnabled() &&
                    (!selected || rigidbody->GetInstanceId() < selected->GetInstanceId()))
                {
                    selected = rigidbody;
                }
            }
            if (selected)
            {
                // 하나의 Transform은 이 단계에서 하나의 속도만 가질 수 있다. 중복 몸체를 모두
                // 움직여 두 번 적분하는 대신, 생성 순서가 가장 이른 하나를 고정해 선택한다.
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

bool Physics2DSystem::AreOverlapping(
    const Collider2D& first, const Math::Aabb2D& firstBounds,
    const Collider2D& second, const Math::Aabb2D& secondBounds)
{
    // 양쪽 모두에게 묻는다. 한쪽만 물으면 타일맵의 빈 자리가 상대의 사각형으로만 판정되어,
    // 아무 타일도 없는 곳에서 겹쳤다고 답한다.
    return firstBounds.Overlaps(secondBounds) && first.OverlapsCollider(second) &&
        second.OverlapsCollider(first);
}

void Physics2DSystem::Simulate(SceneManager& sceneManager, const float deltaTime)
{
    // 큰 멈춤 뒤의 시간을 전부 되갚으려 하면 물리 하나가 다음 프레임의 시간을 또 먹는다. 한
    // 프레임에는 정한 수만큼만 따라가고 나머지는 버려, 앱이 다시 반응하는 쪽을 택한다.
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

    // overlap Enter/Exit는 fixed step의 중간 위치가 아니라 이번 프레임 최종 위치의 일이어야
    // 한다. 물리 스텝이 하나도 없는 빠른 프레임에도 직접 옮긴 Trigger를 위해 반드시 갱신한다.
    static_cast<void>(Synchronize(sceneManager));
}

void Physics2DSystem::SimulateFixedStep(SceneManager& sceneManager)
{
    std::vector<Collider2D*> colliders;
    std::vector<Rigidbody2D*> rigidbodies;
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

    for (Rigidbody2D* const rigidbody : rigidbodies)
    {
        rigidbody->ClearContacts();
        SimulateRigidbody(*rigidbody, colliders);
    }
}

void Physics2DSystem::SimulateRigidbody(
    Rigidbody2D& rigidbody, const std::vector<Collider2D*>& colliders)
{
    rigidbody.SetVelocity(
        rigidbody.GetVelocity() + mGravity * (rigidbody.GetGravityScale() * FixedDeltaTime));

    GameObject* const owner = rigidbody.GetGameObject();
    BoxCollider2D* movingCollider = nullptr;
    if (owner)
    {
        for (BoxCollider2D* const collider : owner->GetComponents<BoxCollider2D>())
        {
            if (collider && collider->IsActiveAndEnabled() && !collider->IsTrigger() &&
                (!movingCollider || collider->GetInstanceId() < movingCollider->GetInstanceId()))
            {
                movingCollider = collider;
            }
        }
    }
    if (!movingCollider)
    {
        // 콜라이더가 없어도 Rigidbody2D는 속도와 중력의 결과를 Transform에 적용한다. Trigger만
        // 붙은 물체도 그 성질상 고체에는 막히지 않는다. BoxCollider2D가 여럿인 설정은 가장 먼저
        // 만들어진 활성 고체 하나만 첫 단계의 움직이는 도형으로 쓴다.
    }

    const Math::Vector2 velocity = rigidbody.GetVelocity();
    MoveAlongAxis(
        rigidbody, movingCollider, colliders, { velocity.GetX() * FixedDeltaTime, 0.0f });
    MoveAlongAxis(
        rigidbody, movingCollider, colliders,
        { 0.0f, rigidbody.GetVelocity().GetY() * FixedDeltaTime });
}

std::optional<Math::Aabb2DSweepHit> Physics2DSystem::FindEarliestHit(
    const Rigidbody2D& rigidbody, const BoxCollider2D& movingCollider,
    const std::vector<Collider2D*>& colliders,
    const Math::Vector2& worldDisplacement) const
{
    if (std::abs(worldDisplacement.GetX()) <= MovementEpsilon &&
        std::abs(worldDisplacement.GetY()) <= MovementEpsilon)
    {
        return std::nullopt;
    }

    const GameObject* const owner = rigidbody.GetGameObject();
    const Math::Aabb2D movingBounds = movingCollider.GetWorldBounds();
    if (!owner || movingBounds.IsEmpty())
    {
        return std::nullopt;
    }

    std::optional<Math::Aabb2DSweepHit> earliest;
    for (Collider2D* const target : colliders)
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

        const std::optional<Math::Aabb2DSweepHit> hit =
            target->SweepBox(movingBounds, worldDisplacement);
        if (hit && (!earliest || hit->fraction < earliest->fraction))
        {
            // colliders는 instance id 순서라 fraction이 같은 벽·타일은 먼저 본 대상이 항상
            // 이긴다. 동률까지 덮어쓰면 unordered 장면 순회에 결과를 맡기게 된다.
            earliest = hit;
        }
    }
    return earliest;
}

void Physics2DSystem::MoveAlongAxis(
    Rigidbody2D& rigidbody, const BoxCollider2D* const movingCollider,
    const std::vector<Collider2D*>& colliders,
    const Math::Vector2& worldDisplacement) const
{
    if (std::abs(worldDisplacement.GetX()) <= MovementEpsilon &&
        std::abs(worldDisplacement.GetY()) <= MovementEpsilon)
    {
        return;
    }

    Transform* const transform = rigidbody.GetTransform();
    if (!transform)
    {
        return;
    }

    float fraction = 1.0f;
    std::optional<Math::Aabb2DSweepHit> hit;
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
        current.GetZ() };
    if (!transform->SetWorldPosition(destination))
    {
        return;
    }

    if (!hit)
    {
        return;
    }

    rigidbody.AddContact(hit->normal);
    Math::Vector2 velocity = rigidbody.GetVelocity();
    if (hit->normal.GetX() != 0.0f)
    {
        velocity = { 0.0f, velocity.GetY() };
    }
    else if (hit->normal.GetY() != 0.0f)
    {
        velocity = { velocity.GetX(), 0.0f };
    }
    rigidbody.SetVelocity(velocity);
}

std::size_t Physics2DSystem::Synchronize(SceneManager& sceneManager)
{
    std::vector<Collider2D*> colliders;
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

    // 품는 사각형은 짝마다 다시 묻지 않는다. 콜라이더가 100개면 짝은 약 5000개이고, 그때마다
    // 계층을 거슬러 월드 행렬을 얻는 것이 이 시스템에서 가장 비싼 일이 된다.
    std::vector<Math::Aabb2D> bounds;
    bounds.reserve(colliders.size());
    for (const Collider2D* const collider : colliders)
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
