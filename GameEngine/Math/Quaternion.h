#pragma once

#include "Vector.h"

namespace GameEngine::Math
{

struct Matrix4x4;

/// <summary>
/// 회전을 나타내는 단위 쿼터니언이다.
///
/// 규약은 이 엔진의 행렬과 같다: 행 벡터, 왼손 좌표계, 오일러 각은 roll(Z)·pitch(X)·yaw(Y)
/// 순으로 적용되는 도(degree) 단위 (pitch, yaw, roll) = (x, y, z). 곱셈 `a * b`는 "a를 먼저,
/// 그 다음 b"다 — 행렬 `A * B`가 그렇듯이 — 그래서 `(a * b).ToMatrix()`는
/// `a.ToMatrix() * b.ToMatrix()`와 같다.
///
/// Transform은 오일러 각을 저장하지만 회전을 합치거나 보간하는 계산은 여기서 하는 편이 짐벌
/// 락이 없고 수치적으로 안정적이다.
/// </summary>
struct Quaternion
{
    constexpr Quaternion() = default;
    constexpr Quaternion(const float x, const float y, const float z, const float w)
        : mX(x), mY(y), mZ(z), mW(w)
    {
    }

    [[nodiscard]] constexpr float GetX() const { return mX; }
    [[nodiscard]] constexpr float GetY() const { return mY; }
    [[nodiscard]] constexpr float GetZ() const { return mZ; }
    [[nodiscard]] constexpr float GetW() const { return mW; }

    [[nodiscard]] static constexpr Quaternion Identity() { return { 0.0f, 0.0f, 0.0f, 1.0f }; }

    /// <summary>축 둘레의 회전이다. 축은 정규화되며, 각도는 도 단위다.</summary>
    [[nodiscard]] static Quaternion FromAxisAngleDegrees(const Vector3& axis, float degrees);

    /// <summary>엔진 규약의 오일러 각(pitch, yaw, roll; 도 단위)에서 만든다.</summary>
    [[nodiscard]] static Quaternion FromEulerDegrees(const Vector3& eulerDegrees);

    /// <summary>행렬의 회전 부분에서 만든다. 배율은 제거되고 이동은 무시된다.</summary>
    [[nodiscard]] static Quaternion FromMatrix(const Matrix4x4& matrix);

    /// <summary>`from` 방향을 `to` 방향으로 돌리는 최소 회전이다.</summary>
    [[nodiscard]] static Quaternion FromToRotation(const Vector3& from, const Vector3& to);

    /// <summary>엔진 규약의 오일러 각(pitch, yaw, roll; 도 단위)이다. 짐벌 락에서는 roll이 0이다.</summary>
    [[nodiscard]] Vector3 ToEulerDegrees() const;

    /// <summary>같은 회전의 4x4 행렬이다. 행 벡터 규약이라 TransformDirection과 함께 쓴다.</summary>
    [[nodiscard]] Matrix4x4 ToMatrix() const;

    /// <summary>벡터를 회전시킨다. `ToMatrix().TransformDirection(v)`와 같다.</summary>
    [[nodiscard]] Vector3 Rotate(const Vector3& vector) const;

    /// <summary>이 회전을 먼저, other를 그 다음에 적용하는 회전이다.</summary>
    [[nodiscard]] Quaternion operator*(const Quaternion& other) const;
    [[nodiscard]] constexpr bool operator==(const Quaternion&) const = default;

    [[nodiscard]] constexpr float Dot(const Quaternion& other) const
    {
        return mX * other.mX + mY * other.mY + mZ * other.mZ + mW * other.mW;
    }
    [[nodiscard]] float GetLength() const;
    [[nodiscard]] Quaternion Normalized() const;
    /// <summary>역회전이다. 단위 쿼터니언이라 켤레와 같다.</summary>
    [[nodiscard]] constexpr Quaternion Inverse() const { return { -mX, -mY, -mZ, mW }; }

    /// <summary>두 회전 사이의 각도다. 도 단위, [0, 180]이다.</summary>
    [[nodiscard]] float AngleTo(const Quaternion& other) const;

    /// <summary>구면 선형 보간이다. 짧은 쪽으로 돌고, t는 0..1로 잘린다.</summary>
    [[nodiscard]] static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t);

private:
    float mX = 0.0f;
    float mY = 0.0f;
    float mZ = 0.0f;
    float mW = 1.0f;
};

}
