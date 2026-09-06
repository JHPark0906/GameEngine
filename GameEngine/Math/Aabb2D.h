#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include "Vector.h"

namespace GameEngine::Math
{

/// <summary>
/// 움직이는 축 정렬 사각형이 고정 사각형에 처음 닿는 자리다. <see cref="fraction"/>은
/// 이동 거리에서의 비율이고, <see cref="normal"/>은 움직인 사각형을 고정 사각형 바깥으로
/// 향하게 하는 법선이다.
/// </summary>
struct Aabb2DSweepHit
{
    float fraction = 0.0f;
    Vector2 normal;
};

/// <summary>
/// 축에 나란한 2차원 사각 영역이다. 두 모서리 — 왼쪽 아래와 오른쪽 위 — 로 표현하며, 겹침
/// 판정이 이 표현에서 곱셈 없이 나온다.
///
/// 컴포넌트도 장면도 모른다. 아는 것은 두 점뿐이라 화면 없이 시험할 수 있고, 그래서 겹침
/// 규칙이 물리 시스템 안에 묻히는 대신 여기서 값으로 고정된다.
/// </summary>
struct Aabb2D
{
    /// <summary>왼쪽 아래 모서리다.</summary>
    Vector2 min;
    /// <summary>오른쪽 위 모서리다.</summary>
    Vector2 max;

    /// <summary>가운데와 전체 크기로 영역을 만든다. 크기의 절반씩 양쪽으로 뻗는다.</summary>
    /// <param name="center">가운데 점이다.</param>
    /// <param name="size">가로세로 전체 길이다. 음수면 빈 영역이 된다.</param>
    [[nodiscard]] static constexpr Aabb2D FromCenterSize(
        const Vector2& center, const Vector2& size)
    {
        const Vector2 half{ size.GetX() * 0.5f, size.GetY() * 0.5f };
        return Aabb2D{ center - half, center + half };
    }

    /// <summary>두 점을 품는 가장 작은 영역이다. 어느 점이 어느 모서리인지 묻지 않는다.</summary>
    [[nodiscard]] static constexpr Aabb2D FromPoints(
        const Vector2& first, const Vector2& second)
    {
        return Aabb2D{
            { first.GetX() < second.GetX() ? first.GetX() : second.GetX(),
              first.GetY() < second.GetY() ? first.GetY() : second.GetY() },
            { first.GetX() > second.GetX() ? first.GetX() : second.GetX(),
              first.GetY() > second.GetY() ? first.GetY() : second.GetY() }
        };
    }

    /// <summary>넓이가 없는 영역인지다. 어떤 점도 품지 않고 무엇과도 겹치지 않는다.</summary>
    [[nodiscard]] constexpr bool IsEmpty() const
    {
        return max.GetX() <= min.GetX() || max.GetY() <= min.GetY();
    }

    [[nodiscard]] constexpr Vector2 GetCenter() const
    {
        return { (min.GetX() + max.GetX()) * 0.5f, (min.GetY() + max.GetY()) * 0.5f };
    }

    [[nodiscard]] constexpr Vector2 GetSize() const
    {
        return { max.GetX() - min.GetX(), max.GetY() - min.GetY() };
    }

    /// <summary>
    /// 두 영역이 겹치는지다. 모서리가 정확히 맞닿기만 한 것은 겹친 것이 아니다 — 나란히 놓인
    /// 타일 두 칸이 서로 겹쳤다고 답하면 어떤 격자도 온통 충돌 상태가 된다.
    /// </summary>
    [[nodiscard]] constexpr bool Overlaps(const Aabb2D& other) const
    {
        return !IsEmpty() && !other.IsEmpty() && min.GetX() < other.max.GetX() &&
            other.min.GetX() < max.GetX() && min.GetY() < other.max.GetY() &&
            other.min.GetY() < max.GetY();
    }

    /// <summary>점 하나를 품는지다. 왼쪽 아래 모서리는 안이고 오른쪽 위 모서리는 밖이다.</summary>
    [[nodiscard]] constexpr bool Contains(const Vector2& point) const
    {
        return point.GetX() >= min.GetX() && point.GetX() < max.GetX() &&
            point.GetY() >= min.GetY() && point.GetY() < max.GetY();
    }

    /// <summary>두 영역을 모두 품는 가장 작은 영역이다. 빈 영역은 상대를 그대로 돌려준다.</summary>
    [[nodiscard]] constexpr Aabb2D UnitedWith(const Aabb2D& other) const
    {
        if (IsEmpty())
        {
            return other;
        }
        if (other.IsEmpty())
        {
            return *this;
        }
        return Aabb2D{
            { min.GetX() < other.min.GetX() ? min.GetX() : other.min.GetX(),
              min.GetY() < other.min.GetY() ? min.GetY() : other.min.GetY() },
            { max.GetX() > other.max.GetX() ? max.GetX() : other.max.GetX(),
              max.GetY() > other.max.GetY() ? max.GetY() : other.max.GetY() }
        };
    }

    /// <summary>
    /// 이 사각형을 <paramref name="displacement"/>만큼 움직일 때
    /// <paramref name="obstacle"/>에 처음 닿는 자리를 구한다. 이미 겹친 상태를 밀어 내는
    /// 기능은 아니다: 어느 쪽으로 나가야 하는지는 속도만으로 정할 수 없어서 물리 해소기가
    /// 따로 다뤄야 한다.
    ///
    /// 닿기만 한 것은 <see cref="Overlaps"/>와 마찬가지로 겹침이 아니지만, 닿은 뒤 안쪽으로
    /// 움직이는 경우에는 fraction 0의 충돌이다. 그래서 바닥 위에 선 몸체가 다음 스텝에 바닥을
    /// 지나치지 않는다.
    /// </summary>
    /// <param name="obstacle">움직이지 않는 대상 사각형이다.</param>
    /// <param name="displacement">이 사각형이 이번 스텝에 갈 월드 거리다.</param>
    /// <returns>0부터 1 사이의 최초 충돌과 법선, 또는 경로에 충돌이 없으면 nullopt다.</returns>
    [[nodiscard]] std::optional<Aabb2DSweepHit> SweepAgainst(
        const Aabb2D& obstacle, const Vector2& displacement) const
    {
        constexpr float epsilon = 0.000001f;
        if (IsEmpty() || obstacle.IsEmpty() ||
            !std::isfinite(displacement.GetX()) || !std::isfinite(displacement.GetY()) ||
            (std::abs(displacement.GetX()) <= epsilon &&
             std::abs(displacement.GetY()) <= epsilon))
        {
            return std::nullopt;
        }

        const auto calculateAxis = [epsilon](
            const float movingMin, const float movingMax,
            const float obstacleMin, const float obstacleMax, const float delta,
            float& entry, float& exit)
        {
            if (std::abs(delta) <= epsilon)
            {
                // 이 축에서 떨어져 있으면 다른 축으로 아무리 움직여도 만날 수 없다. 닿기만 한
                // 것은 영역을 공유하지 않는다는 Aabb2D의 같은 규칙을 쓴다.
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
        if (!calculateAxis(
                min.GetX(), max.GetX(), obstacle.min.GetX(), obstacle.max.GetX(),
                displacement.GetX(), entryX, exitX) ||
            !calculateAxis(
                min.GetY(), max.GetY(), obstacle.min.GetY(), obstacle.max.GetY(),
                displacement.GetY(), entryY, exitY))
        {
            return std::nullopt;
        }

        const float entry = (std::max)(entryX, entryY);
        const float exit = (std::min)(exitX, exitY);
        if (entry > exit || exit <= 0.0f || entry > 1.0f)
        {
            return std::nullopt;
        }

        Aabb2DSweepHit hit;
        hit.fraction = (std::max)(entry, 0.0f);
        // 모서리를 정확히 겨냥했으면 더 큰 이동 성분을 우선한다. 동률도 X로 고정해 실행마다
        // 법선이 달라지지 않게 한다.
        if (entryX > entryY ||
            (entryX == entryY && std::abs(displacement.GetX()) >= std::abs(displacement.GetY())))
        {
            hit.normal = { displacement.GetX() > 0.0f ? -1.0f : 1.0f, 0.0f };
        }
        else
        {
            hit.normal = { 0.0f, displacement.GetY() > 0.0f ? -1.0f : 1.0f };
        }
        return hit;
    }

    [[nodiscard]] constexpr bool operator==(const Aabb2D&) const = default;
};

}
