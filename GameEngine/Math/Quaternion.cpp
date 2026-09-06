#include "pch.h"
#include "Quaternion.h"

#include <algorithm>
#include <cmath>

#include "MathUtility.h"
#include "Matrix.h"

namespace GameEngine::Math
{

Quaternion Quaternion::FromAxisAngleDegrees(const Vector3& axis, const float degrees)
{
    const Vector3 unit = axis.Normalized();
    const float half = ToRadians(degrees) * 0.5f;
    const float sinHalf = std::sin(half);
    return { unit.GetX() * sinHalf, unit.GetY() * sinHalf, unit.GetZ() * sinHalf, std::cos(half) };
}

Quaternion Quaternion::FromEulerDegrees(const Vector3& eulerDegrees)
{
    // 행렬이 규약의 정본이다: 같은 각도로 만든 행렬에서 읽으면 두 표현이 어긋날 수 없다.
    return FromMatrix(Matrix4x4::CreateRotationRollPitchYawDegrees(eulerDegrees));
}

Quaternion Quaternion::FromMatrix(const Matrix4x4& matrix)
{
    // 행 벡터 규약: 행 i가 축 i의 상이다. 배율을 나눠 순수 회전을 얻는다.
    float rows[3][3];
    for (int row = 0; row < 3; ++row)
    {
        const Vector3 axis{
            matrix.GetElement(row, 0), matrix.GetElement(row, 1), matrix.GetElement(row, 2) };
        const Vector3 unit = axis.Normalized();
        rows[row][0] = unit.GetX();
        rows[row][1] = unit.GetY();
        rows[row][2] = unit.GetZ();
    }

    // 열 벡터 규약의 표준 추출을 전치된 원소로 적용한다: m[c][r] = rows[r][c].
    const float m00 = rows[0][0], m11 = rows[1][1], m22 = rows[2][2];
    const float trace = m00 + m11 + m22;
    float x, y, z, w;
    if (trace > 0.0f)
    {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        w = 0.25f * s;
        x = (rows[1][2] - rows[2][1]) / s;
        y = (rows[2][0] - rows[0][2]) / s;
        z = (rows[0][1] - rows[1][0]) / s;
    }
    else if (m00 > m11 && m00 > m22)
    {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        w = (rows[1][2] - rows[2][1]) / s;
        x = 0.25f * s;
        y = (rows[1][0] + rows[0][1]) / s;
        z = (rows[2][0] + rows[0][2]) / s;
    }
    else if (m11 > m22)
    {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        w = (rows[2][0] - rows[0][2]) / s;
        x = (rows[1][0] + rows[0][1]) / s;
        y = 0.25f * s;
        z = (rows[2][1] + rows[1][2]) / s;
    }
    else
    {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        w = (rows[0][1] - rows[1][0]) / s;
        x = (rows[2][0] + rows[0][2]) / s;
        y = (rows[2][1] + rows[1][2]) / s;
        z = 0.25f * s;
    }
    return Quaternion{ x, y, z, w }.Normalized();
}

Quaternion Quaternion::FromToRotation(const Vector3& from, const Vector3& to)
{
    const Vector3 a = from.Normalized();
    const Vector3 b = to.Normalized();
    const float cosine = std::clamp(a.Dot(b), -1.0f, 1.0f);
    if (cosine > 1.0f - Epsilon)
    {
        return Identity();
    }
    if (cosine < -1.0f + Epsilon)
    {
        // 정반대 방향: 어느 수직 축으로든 180도. a와 가장 덜 평행한 기저 축을 고른다.
        const Vector3 helper = std::abs(a.GetX()) < 0.9f ? Vector3::Right : Vector3::Up;
        return FromAxisAngleDegrees(a.Cross(helper), 180.0f);
    }
    const Vector3 axis = a.Cross(b);
    const float halfCos = std::sqrt((1.0f + cosine) * 0.5f);
    const float scale = std::sqrt((1.0f - cosine) * 0.5f) / axis.GetLength();
    return Quaternion{ axis.GetX() * scale, axis.GetY() * scale, axis.GetZ() * scale, halfCos }
        .Normalized();
}

Vector3 Quaternion::ToEulerDegrees() const
{
    Vector3 scale;
    Vector3 euler;
    Vector3 translation;
    ToMatrix().Decompose(scale, euler, translation);
    return euler;
}

Matrix4x4 Quaternion::ToMatrix() const
{
    const float xx = mX * mX, yy = mY * mY, zz = mZ * mZ;
    const float xy = mX * mY, xz = mX * mZ, yz = mY * mZ;
    const float wx = mW * mX, wy = mW * mY, wz = mW * mZ;
    // 열 벡터 규약의 표준 회전 행렬 R_c를 전치한 것이 행 벡터 규약의 행렬이다: v' = v * R_c^T.
    return Matrix4x4::FromRows(
        { 1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy) },
        { 2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx) },
        { 2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy) },
        Vector3::Zero);
}

Vector3 Quaternion::Rotate(const Vector3& vector) const
{
    // v' = v + 2w(q × v) + 2(q × (q × v)), q = (x, y, z)
    const Vector3 q{ mX, mY, mZ };
    const Vector3 t = q.Cross(vector) * 2.0f;
    return vector + t * mW + q.Cross(t);
}

Quaternion Quaternion::operator*(const Quaternion& other) const
{
    // "this 먼저, other 다음"은 해밀턴 곱 other ⊗ this이다.
    const Quaternion& a = other;
    const Quaternion& b = *this;
    return {
        a.mW * b.mX + a.mX * b.mW + a.mY * b.mZ - a.mZ * b.mY,
        a.mW * b.mY - a.mX * b.mZ + a.mY * b.mW + a.mZ * b.mX,
        a.mW * b.mZ + a.mX * b.mY - a.mY * b.mX + a.mZ * b.mW,
        a.mW * b.mW - a.mX * b.mX - a.mY * b.mY - a.mZ * b.mZ,
    };
}

float Quaternion::GetLength() const
{
    return std::sqrt(Dot(*this));
}

Quaternion Quaternion::Normalized() const
{
    const float length = GetLength();
    if (length <= Epsilon)
    {
        return Identity();
    }
    return { mX / length, mY / length, mZ / length, mW / length };
}

float Quaternion::AngleTo(const Quaternion& other) const
{
    const float cosine = std::clamp(std::abs(Dot(other)), 0.0f, 1.0f);
    return ToDegrees(2.0f * std::acos(cosine));
}

Quaternion Quaternion::Slerp(const Quaternion& a, const Quaternion& b, const float t)
{
    const float clampedT = Clamp01(t);
    float cosine = a.Dot(b);
    // 부호를 맞춰 짧은 쪽으로 돈다. q와 -q는 같은 회전이다.
    Quaternion target = b;
    if (cosine < 0.0f)
    {
        cosine = -cosine;
        target = { -b.mX, -b.mY, -b.mZ, -b.mW };
    }
    float weightA;
    float weightB;
    if (cosine > 1.0f - 0.001f)
    {
        // 거의 같은 방향: 선형 보간으로 충분하고, sin이 0에 가까워 나눗셈이 불안하다.
        weightA = 1.0f - clampedT;
        weightB = clampedT;
    }
    else
    {
        const float angle = std::acos(cosine);
        const float sine = std::sin(angle);
        weightA = std::sin((1.0f - clampedT) * angle) / sine;
        weightB = std::sin(clampedT * angle) / sine;
    }
    return Quaternion{
        a.mX * weightA + target.mX * weightB,
        a.mY * weightA + target.mY * weightB,
        a.mZ * weightA + target.mZ * weightB,
        a.mW * weightA + target.mW * weightB,
    }.Normalized();
}

}
