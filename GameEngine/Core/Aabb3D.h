#pragma once

#include "../Math/Matrix.h"
#include "../Math/Vector.h"

namespace GameEngine::Core
{

/// <summary>
/// 축에 나란한 3차원 상자다. 두 모서리 — 각 축의 최소와 최대 — 로 표현하며, 형상을 감싸는
/// 가장 작은 상자를 정점에서 한 번 얻어 두는 자리다.
///
/// <b><see cref="Aabb2D"/>와 이름이 닮았지만 「비었다」의 뜻이 다르다.</b> 2차원 쪽은 넓이가
/// 없는 것을 빈 것으로 보지만(맞닿기만 한 두 타일이 겹쳤다고 답하면 격자가 온통 충돌이 된다),
/// 여기서는 <b>한 축이 납작한 것은 비어 있지 않다</b>. 평면 위의 사각형 메시는 z가 전부 같아
/// 두께가 0인데 그것은 엄연히 그릴 것이 있는 형상이고, 이 엔진에서는 오히려 흔한 쪽이다.
/// 납작한 것을 비었다고 답하면 그 메시들이 화면에서 사라진다.
///
/// 그래서 비어 있는 상자는 <see cref="Empty"/>가 만드는 것 하나뿐이다 — <b>아무 점도 넣지
/// 않은</b> 상자. 그 상태에서 시작해 점을 하나씩 넣으면 별도의 「첫 점인가」 검사 없이 감싸는
/// 상자가 된다.
///
/// 컴포넌트도 장면도 모른다. 아는 것은 두 점뿐이라 화면 없이 시험할 수 있다.
/// </summary>
struct Aabb3D
{
    /// <summary>각 축의 최솟값이다.</summary>
    Math::Vector3 min;
    /// <summary>각 축의 최댓값이다.</summary>
    Math::Vector3 max;

    /// <summary>가운데와 전체 크기로 상자를 만든다. 크기의 절반씩 세 축 양쪽으로 뻗는다.</summary>
    /// <param name="center">상자의 가운데 점이다.</param>
    /// <param name="size">세 축의 전체 길이다. 음수 축이 있으면 비어 있는 상자가 된다.</param>
    [[nodiscard]] static constexpr Aabb3D FromCenterSize(
        const Math::Vector3& center, const Math::Vector3& size)
    {
        const Math::Vector3 half{ size.GetX() * 0.5f, size.GetY() * 0.5f, size.GetZ() * 0.5f };
        return Aabb3D{ center - half, center + half };
    }

    /// <summary>두 점을 품는 가장 작은 상자다. 어느 점이 어느 모서리인지 묻지 않는다.</summary>
    [[nodiscard]] static constexpr Aabb3D FromPoints(
        const Math::Vector3& first, const Math::Vector3& second)
    {
        return Aabb3D{
            { first.GetX() < second.GetX() ? first.GetX() : second.GetX(),
              first.GetY() < second.GetY() ? first.GetY() : second.GetY(),
              first.GetZ() < second.GetZ() ? first.GetZ() : second.GetZ() },
            { first.GetX() > second.GetX() ? first.GetX() : second.GetX(),
              first.GetY() > second.GetY() ? first.GetY() : second.GetY(),
              first.GetZ() > second.GetZ() ? first.GetZ() : second.GetZ() }
        };
    }

    /// <summary>
    /// 아무 점도 넣지 않은 상자다. 모서리가 뒤집혀 있어, 어떤 점을 넣어도 그 점 하나짜리
    /// 상자가 되는 것이 <see cref="Encapsulate"/>의 출발점이다.
    /// </summary>
    [[nodiscard]] static constexpr Aabb3D Empty()
    {
        constexpr float Big = 3.402823466e+38f;
        return Aabb3D{ { Big, Big, Big }, { -Big, -Big, -Big } };
    }

    /// <summary>점 하나가 들어오도록 상자를 넓힌다. 이미 안이면 그대로다.</summary>
    constexpr void Encapsulate(const Math::Vector3& point)
    {
        min = { point.GetX() < min.GetX() ? point.GetX() : min.GetX(),
                point.GetY() < min.GetY() ? point.GetY() : min.GetY(),
                point.GetZ() < min.GetZ() ? point.GetZ() : min.GetZ() };
        max = { point.GetX() > max.GetX() ? point.GetX() : max.GetX(),
                point.GetY() > max.GetY() ? point.GetY() : max.GetY(),
                point.GetZ() > max.GetZ() ? point.GetZ() : max.GetZ() };
    }

    /// <summary>
    /// 아무 점도 넣지 않은 상자인지다. <b>납작한 것도 점 하나짜리도 비어 있지 않다</b> —
    /// 클래스 설명에 적힌 이유로, 여기서 비었다는 것은 「감쌀 것이 없었다」는 뜻뿐이다.
    /// </summary>
    [[nodiscard]] constexpr bool IsEmpty() const
    {
        return max.GetX() < min.GetX() || max.GetY() < min.GetY() || max.GetZ() < min.GetZ();
    }

    /// <summary>
    /// 세 축 모두 양의 길이를 갖는, 퇴화하지 않은 입체인지다. <see cref="IsEmpty"/>와 달리
    /// 평면·선·점은 거짓으로 답한다. 비어 있지는 않아도 3차원 부피를 갖지 않는 경계를 가르는
    /// 순수 기하 질의다.
    /// </summary>
    [[nodiscard]] constexpr bool HasVolume() const
    {
        return min.GetX() < max.GetX() && min.GetY() < max.GetY() && min.GetZ() < max.GetZ();
    }

    [[nodiscard]] constexpr Math::Vector3 GetCenter() const
    {
        return { (min.GetX() + max.GetX()) * 0.5f, (min.GetY() + max.GetY()) * 0.5f,
                 (min.GetZ() + max.GetZ()) * 0.5f };
    }

    [[nodiscard]] constexpr Math::Vector3 GetSize() const
    {
        return { max.GetX() - min.GetX(), max.GetY() - min.GetY(), max.GetZ() - min.GetZ() };
    }

    /// <summary>
    /// 두 퇴화하지 않은 입체가 내부를 공유하는지다. 면이나 모서리만 정확히 맞닿은 것은 겹침이
    /// 아니다. 세 축 모두에서 열린 구간이 만나야 하므로, 평면·선·점 상자는 참을 만들지 않는다.
    /// </summary>
    [[nodiscard]] constexpr bool Overlaps(const Aabb3D& other) const
    {
        return HasVolume() && other.HasVolume() && min.GetX() < other.max.GetX() &&
            other.min.GetX() < max.GetX() && min.GetY() < other.max.GetY() &&
            other.min.GetY() < max.GetY() && min.GetZ() < other.max.GetZ() &&
            other.min.GetZ() < max.GetZ();
    }

    /// <summary>점 하나를 품는지다. 최소 모서리는 안이고 최대 모서리는 밖이다.</summary>
    [[nodiscard]] constexpr bool Contains(const Math::Vector3& point) const
    {
        return point.GetX() >= min.GetX() && point.GetX() < max.GetX() &&
            point.GetY() >= min.GetY() && point.GetY() < max.GetY() &&
            point.GetZ() >= min.GetZ() && point.GetZ() < max.GetZ();
    }

    /// <summary>두 상자를 모두 품는 가장 작은 상자다. 빈 상자는 상대를 그대로 돌려준다.</summary>
    [[nodiscard]] constexpr Aabb3D UnitedWith(const Aabb3D& other) const
    {
        if (IsEmpty())
        {
            return other;
        }
        if (other.IsEmpty())
        {
            return *this;
        }
        Aabb3D united = *this;
        united.Encapsulate(other.min);
        united.Encapsulate(other.max);
        return united;
    }

    /// <summary>
    /// 이 상자를 그 행렬로 옮긴 뒤 다시 감싼 상자다. 여덟 꼭짓점을 모두 옮기고 다시 감싸는
    /// 이유는 행렬이 회전을 실을 수 있어서다 — 회전한 상자는 축에 나란하지 않으므로, 중심과
    /// 크기만 옮기면 회전분만큼 상자가 작게 나온다. 여덟 꼭짓점을 다시 감싸면 항상 그 회전한
    /// 상자를 온전히 덮는 축 정렬 상자가 된다.
    /// </summary>
    [[nodiscard]] Aabb3D TransformedBy(const Math::Matrix4x4& matrix) const
    {
        if (IsEmpty())
        {
            return Empty();
        }
        Aabb3D result = Empty();
        for (const float x : { min.GetX(), max.GetX() })
        {
            for (const float y : { min.GetY(), max.GetY() })
            {
                for (const float z : { min.GetZ(), max.GetZ() })
                {
                    result.Encapsulate(matrix.TransformPoint({ x, y, z }));
                }
            }
        }
        return result;
    }

    [[nodiscard]] constexpr bool operator==(const Aabb3D&) const = default;
};

}
