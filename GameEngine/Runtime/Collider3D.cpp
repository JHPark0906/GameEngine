#include "pch.h"
#include "Collider3D.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <utility>

#include "GameObject.h"
#include "PropertyDescriptor.h"
#include "Transform.h"

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// 움직이는 상자와 고정 상자의 첫 충돌을 물리 계층에서 구한다. Aabb3D는 경계·변환 같은 순수
    /// 기하 연산만 두고, 접촉 시점·법선처럼 시뮬레이션이 해석하는 결과는 콜라이더 쪽에 둔다.
    /// </summary>
    [[nodiscard]] std::optional<Collider3DSweepHit> SweepAabb(
        const Core::Aabb3D& movingBox, const Core::Aabb3D& obstacle,
        const Math::Vector3& displacement)
    {
        constexpr float Epsilon = 0.000001f;
        if (!movingBox.HasVolume() || !obstacle.HasVolume() ||
            !std::isfinite(displacement.GetX()) || !std::isfinite(displacement.GetY()) ||
            !std::isfinite(displacement.GetZ()) ||
            (std::abs(displacement.GetX()) <= Epsilon &&
             std::abs(displacement.GetY()) <= Epsilon &&
             std::abs(displacement.GetZ()) <= Epsilon))
        {
            return std::nullopt;
        }

        const auto CalculateAxis = [Epsilon](
            const float movingMin, const float movingMax,
            const float obstacleMin, const float obstacleMax, const float delta,
            float& entry, float& exit)
        {
            if (std::abs(delta) <= Epsilon)
            {
                if (movingMax <= obstacleMin || obstacleMax <= movingMin)
                {
                    return false;
                }
                entry = -std::numeric_limits<float>::infinity();
                exit = std::numeric_limits<float>::infinity();
                return true;
            }

            const float first = (obstacleMin - movingMax) / delta;
            const float second = (obstacleMax - movingMin) / delta;
            entry = (std::min)(first, second);
            exit = (std::max)(first, second);
            return true;
        };

        float entryX = 0.0f;
        float exitX = 0.0f;
        float entryY = 0.0f;
        float exitY = 0.0f;
        float entryZ = 0.0f;
        float exitZ = 0.0f;
        if (!CalculateAxis(
                movingBox.min.GetX(), movingBox.max.GetX(), obstacle.min.GetX(), obstacle.max.GetX(),
                displacement.GetX(), entryX, exitX) ||
            !CalculateAxis(
                movingBox.min.GetY(), movingBox.max.GetY(), obstacle.min.GetY(), obstacle.max.GetY(),
                displacement.GetY(), entryY, exitY) ||
            !CalculateAxis(
                movingBox.min.GetZ(), movingBox.max.GetZ(), obstacle.min.GetZ(), obstacle.max.GetZ(),
                displacement.GetZ(), entryZ, exitZ))
        {
            return std::nullopt;
        }

        const float entry = (std::max)((std::max)(entryX, entryY), entryZ);
        const float exit = (std::min)((std::min)(exitX, exitY), exitZ);
        if (entry > exit || exit <= 0.0f || entry > 1.0f)
        {
            return std::nullopt;
        }

        Collider3DSweepHit hit;
        hit.fraction = (std::max)(entry, 0.0f);
        // 모서리·모퉁이를 정확히 향하면 더 큰 이동 성분을 택하고, 그것도 같으면 X, Y, Z 순서로
        // 고정한다. 2D의 X/Y 규칙을 Z까지 늘린 것으로, 같은 입력에서 접촉면이 흔들리지 않는다.
        float largestEntry = entryX;
        float largestMovement = std::abs(displacement.GetX());
        unsigned int axis = 0;
        if (entryY > largestEntry ||
            (entryY == largestEntry && std::abs(displacement.GetY()) > largestMovement))
        {
            largestEntry = entryY;
            largestMovement = std::abs(displacement.GetY());
            axis = 1;
        }
        if (entryZ > largestEntry ||
            (entryZ == largestEntry && std::abs(displacement.GetZ()) > largestMovement))
        {
            axis = 2;
        }

        if (axis == 0)
        {
            hit.normal = { displacement.GetX() > 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f };
        }
        else if (axis == 1)
        {
            hit.normal = { 0.0f, displacement.GetY() > 0.0f ? -1.0f : 1.0f, 0.0f };
        }
        else
        {
            hit.normal = { 0.0f, 0.0f, displacement.GetZ() > 0.0f ? -1.0f : 1.0f };
        }
        return hit;
    }

    /// <summary>Collider3D가 선언하는 속성이다. 프레임마다 바뀌는 겹침 목록은 저장하지 않는다.</summary>
    std::span<const PropertyDescriptor> Collider3DProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<Collider3D>(
                "isTrigger", "Is Trigger", &Collider3D::IsTrigger, &Collider3D::SetTrigger),
        };
        return properties;
    }
}

const ComponentType& Collider3D::StaticType()
{
    static const ComponentType type{
        "Collider3D", &Behaviour::StaticType(), &Collider3DProperties };
    return type;
}

bool Collider3D::IsOverlapping(const unsigned int instanceId) const
{
    return std::ranges::find(mOverlapping, instanceId) != mOverlapping.end();
}

Core::Aabb3D Collider3D::TransformToWorld(const Core::Aabb3D& localBounds) const
{
    const GameObject* const owner = GetGameObject();
    if (!owner || !localBounds.HasVolume())
    {
        return localBounds;
    }
    return localBounds.TransformedBy(owner->GetTransform().GetLocalToWorldMatrix());
}

std::optional<Collider3DSweepHit> Collider3D::SweepBox(
    const Core::Aabb3D& movingBox, const Math::Vector3& worldDisplacement) const
{
    return SweepAabb(movingBox, GetWorldBounds(), worldDisplacement);
}

void Collider3D::ApplyOverlaps(std::vector<unsigned int> overlapping)
{
    std::ranges::sort(overlapping);
    for (const unsigned int id : overlapping)
    {
        if (std::ranges::find(mOverlapping, id) == mOverlapping.end())
        {
            mEntered.push_back(id);
        }
    }
    for (const unsigned int id : mOverlapping)
    {
        if (std::ranges::find(overlapping, id) == overlapping.end())
        {
            mExited.push_back(id);
        }
    }
    mOverlapping = std::move(overlapping);
}

void Collider3D::ClearFrameFlags()
{
    mEntered.clear();
    mExited.clear();
}

}
