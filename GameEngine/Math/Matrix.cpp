#include "pch.h"
#include "Matrix.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <utility>

#include "MathUtility.h"
#include "Quaternion.h"

namespace GameEngine::Math
{

namespace
{
    constexpr float InversionEpsilon = Epsilon;
}

Matrix4x4 Matrix4x4::FromRows(
    const Vector3& row0, const Vector3& row1, const Vector3& row2, const Vector3& translation)
{
    return Matrix4x4({
        row0.GetX(), row0.GetY(), row0.GetZ(), 0.0f,
        row1.GetX(), row1.GetY(), row1.GetZ(), 0.0f,
        row2.GetX(), row2.GetY(), row2.GetZ(), 0.0f,
        translation.GetX(), translation.GetY(), translation.GetZ(), 1.0f
    });
}

Matrix4x4 Matrix4x4::CreateRotation(const Quaternion& rotation)
{
    return rotation.ToMatrix();
}

Matrix4x4 Matrix4x4::CreateTransform(
    const Vector3& translation, const Vector3& rotationDegrees, const Vector3& scale)
{
    return CreateScale(scale) * CreateRotationRollPitchYawDegrees(rotationDegrees) *
        CreateTranslation(translation);
}

void Matrix4x4::Decompose(Vector3& scale, Vector3& rotationDegrees, Vector3& translation) const
{
    // 행 벡터 규약: 위 세 행이 배율이 곱해진 기저 벡터, 넷째 행이 이동이다. 배율은 각 행의
    // 길이이고, 그것을 나눈 순수 회전에서 CreateRotationRollPitchYawDegrees의 배치를 거꾸로
    // 읽어 각도를 얻는다.
    float rows[3][3];
    float lengths[3];
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            rows[row][column] = GetElement(row, column);
        }
        lengths[row] = std::sqrt(
            rows[row][0] * rows[row][0] + rows[row][1] * rows[row][1] +
            rows[row][2] * rows[row][2]);
        if (lengths[row] > Epsilon)
        {
            for (int column = 0; column < 3; ++column)
            {
                rows[row][column] /= lengths[row];
            }
        }
    }

    // 반사는 회전만으로 표현할 수 없다. 부호의 배정은 유일하지 않으므로 X에 모으고,
    // 회전 기저의 행렬식을 양수로 만든 뒤 각도로 되돌린다. 재합성한 행렬은 원래와 같다.
    const float orientation =
        rows[0][0] * (rows[1][1] * rows[2][2] - rows[1][2] * rows[2][1]) -
        rows[0][1] * (rows[1][0] * rows[2][2] - rows[1][2] * rows[2][0]) +
        rows[0][2] * (rows[1][0] * rows[2][1] - rows[1][1] * rows[2][0]);
    if (orientation < 0.0f)
    {
        lengths[0] = -lengths[0];
        for (float& element : rows[0]) element = -element;
    }

    const float sinPitch = std::clamp(-rows[2][1], -1.0f, 1.0f);
    const float pitch = std::asin(sinPitch);
    float yaw;
    float roll;
    if (std::abs(sinPitch) < 0.9999f)
    {
        yaw = std::atan2(rows[2][0], rows[2][2]);
        roll = std::atan2(rows[0][1], rows[1][1]);
    }
    else
    {
        // 짐벌 락: 요와 롤이 한 축으로 합쳐지므로 롤을 0으로 두고 요만 남긴다.
        yaw = std::atan2(-rows[0][2], rows[0][0]);
        roll = 0.0f;
    }

    scale = { lengths[0], lengths[1], lengths[2] };
    rotationDegrees = { ToDegrees(pitch), ToDegrees(yaw), ToDegrees(roll) };
    translation = GetTranslation();
}

Vector3 Matrix4x4::GetTranslation() const
{
    return { GetElement(3, 0), GetElement(3, 1), GetElement(3, 2) };
}

Matrix4x4 Matrix4x4::Identity()
{
    return {};
}

Matrix4x4 Matrix4x4::CreateScale(const Vector3& scale)
{
    return Matrix4x4({
        scale.GetX(), 0.0f, 0.0f, 0.0f,
        0.0f, scale.GetY(), 0.0f, 0.0f,
        0.0f, 0.0f, scale.GetZ(), 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    });
}

Matrix4x4 Matrix4x4::CreateTranslation(const Vector3& translation)
{
    return Matrix4x4({
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        translation.GetX(), translation.GetY(), translation.GetZ(), 1.0f
    });
}

Matrix4x4 Matrix4x4::CreateRotationRollPitchYawDegrees(const Vector3& rotation)
{
    const float pitch = rotation.GetX() * DegreesToRadians;
    const float yaw = rotation.GetY() * DegreesToRadians;
    const float roll = rotation.GetZ() * DegreesToRadians;
    const float sinPitch = std::sin(pitch);
    const float cosPitch = std::cos(pitch);
    const float sinYaw = std::sin(yaw);
    const float cosYaw = std::cos(yaw);
    const float sinRoll = std::sin(roll);
    const float cosRoll = std::cos(roll);

    return Matrix4x4({
        cosRoll * cosYaw + sinRoll * sinPitch * sinYaw,
        sinRoll * cosPitch,
        sinRoll * sinPitch * cosYaw - cosRoll * sinYaw,
        0.0f,
        cosRoll * sinPitch * sinYaw - sinRoll * cosYaw,
        cosRoll * cosPitch,
        sinRoll * sinYaw + cosRoll * sinPitch * cosYaw,
        0.0f,
        cosPitch * sinYaw,
        -sinPitch,
        cosPitch * cosYaw,
        0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    });
}

Matrix4x4 Matrix4x4::CreateRotationXDegrees(const float degrees)
{
    const float angle = degrees * DegreesToRadians;
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    return Matrix4x4({
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, cosine, sine, 0.0f,
        0.0f, -sine, cosine, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    });
}

Matrix4x4 Matrix4x4::CreateRotationYDegrees(const float degrees)
{
    const float angle = degrees * DegreesToRadians;
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    return Matrix4x4({
        cosine, 0.0f, -sine, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        sine, 0.0f, cosine, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    });
}

Matrix4x4 Matrix4x4::CreateRotationZDegrees(const float degrees)
{
    const float angle = degrees * DegreesToRadians;
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    return Matrix4x4({
        cosine, sine, 0.0f, 0.0f,
        -sine, cosine, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    });
}

Matrix4x4 Matrix4x4::CreatePerspectiveFieldOfViewLeftHanded(
    const float fieldOfViewDegrees,
    const float aspectRatio,
    const float nearClipPlane,
    const float farClipPlane)
{
    const float yScale = 1.0f / std::tan(fieldOfViewDegrees * DegreesToRadians * 0.5f);
    const float depth = farClipPlane / (farClipPlane - nearClipPlane);
    return Matrix4x4({
        yScale / aspectRatio, 0.0f, 0.0f, 0.0f,
        0.0f, yScale, 0.0f, 0.0f,
        0.0f, 0.0f, depth, 1.0f,
        0.0f, 0.0f, -nearClipPlane * depth, 0.0f
    });
}

Matrix4x4 Matrix4x4::CreateOrthographicLeftHanded(
    const float width,
    const float height,
    const float nearClipPlane,
    const float farClipPlane)
{
    const float inverseDepth = 1.0f / (farClipPlane - nearClipPlane);
    return Matrix4x4({
        2.0f / width, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / height, 0.0f, 0.0f,
        0.0f, 0.0f, inverseDepth, 0.0f,
        0.0f, 0.0f, -nearClipPlane * inverseDepth, 1.0f
    });
}

Matrix4x4 Matrix4x4::CreateOrthographicOffCenterLeftHanded(
    const float left,
    const float right,
    const float bottom,
    const float top,
    const float nearClipPlane,
    const float farClipPlane)
{
    const float inverseWidth = 1.0f / (right - left);
    const float inverseHeight = 1.0f / (top - bottom);
    const float inverseDepth = 1.0f / (farClipPlane - nearClipPlane);
    return Matrix4x4({
        2.0f * inverseWidth, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f * inverseHeight, 0.0f, 0.0f,
        0.0f, 0.0f, inverseDepth, 0.0f,
        -(left + right) * inverseWidth, -(top + bottom) * inverseHeight,
        -nearClipPlane * inverseDepth, 1.0f
    });
}

float Matrix4x4::GetElement(const std::size_t row, const std::size_t column) const
{
    assert(row < 4 && column < 4);
    return mElements[row * 4 + column];
}

bool Matrix4x4::IsFinite() const
{
    for (const float element : mElements)
    {
        if (!std::isfinite(element)) return false;
    }
    return true;
}

Matrix4x4 Matrix4x4::operator*(const Matrix4x4& other) const
{
    std::array<float, 16> result{};
    for (std::size_t row = 0; row < 4; ++row)
    for (std::size_t column = 0; column < 4; ++column)
    for (std::size_t index = 0; index < 4; ++index)
    {
        result[row * 4 + column] += GetElement(row, index) * other.GetElement(index, column);
    }
    return Matrix4x4(result);
}

Vector3 Matrix4x4::TransformPoint(const Vector3& point) const
{
    const float x = point.GetX();
    const float y = point.GetY();
    const float z = point.GetZ();
    const float transformedX = x * GetElement(0, 0) + y * GetElement(1, 0) + z * GetElement(2, 0) + GetElement(3, 0);
    const float transformedY = x * GetElement(0, 1) + y * GetElement(1, 1) + z * GetElement(2, 1) + GetElement(3, 1);
    const float transformedZ = x * GetElement(0, 2) + y * GetElement(1, 2) + z * GetElement(2, 2) + GetElement(3, 2);
    const float transformedW = x * GetElement(0, 3) + y * GetElement(1, 3) + z * GetElement(2, 3) + GetElement(3, 3);
    if (std::abs(transformedW) <= InversionEpsilon)
    {
        return { transformedX, transformedY, transformedZ };
    }
    return { transformedX / transformedW, transformedY / transformedW, transformedZ / transformedW };
}

Vector3 Matrix4x4::TransformDirection(const Vector3& direction) const
{
    const float x = direction.GetX();
    const float y = direction.GetY();
    const float z = direction.GetZ();
    return {
        x * GetElement(0, 0) + y * GetElement(1, 0) + z * GetElement(2, 0),
        x * GetElement(0, 1) + y * GetElement(1, 1) + z * GetElement(2, 1),
        x * GetElement(0, 2) + y * GetElement(1, 2) + z * GetElement(2, 2)
    };
}

Matrix4x4 Matrix4x4::Transpose() const
{
    std::array<float, 16> elements{};
    for (std::size_t row = 0; row < 4; ++row)
    for (std::size_t column = 0; column < 4; ++column)
    {
        elements[row * 4 + column] = GetElement(column, row);
    }
    return Matrix4x4(elements);
}

float Matrix4x4::GetDeterminant() const
{
    // Laplace expansion along the first row, with the 3x3 minors written out.
    const auto minor = [this](
        const std::size_t r1, const std::size_t r2, const std::size_t r3,
        const std::size_t c1, const std::size_t c2, const std::size_t c3)
    {
        return GetElement(r1, c1) *
                (GetElement(r2, c2) * GetElement(r3, c3) - GetElement(r2, c3) * GetElement(r3, c2)) -
            GetElement(r1, c2) *
                (GetElement(r2, c1) * GetElement(r3, c3) - GetElement(r2, c3) * GetElement(r3, c1)) +
            GetElement(r1, c3) *
                (GetElement(r2, c1) * GetElement(r3, c2) - GetElement(r2, c2) * GetElement(r3, c1));
    };

    return GetElement(0, 0) * minor(1, 2, 3, 1, 2, 3) -
        GetElement(0, 1) * minor(1, 2, 3, 0, 2, 3) +
        GetElement(0, 2) * minor(1, 2, 3, 0, 1, 3) -
        GetElement(0, 3) * minor(1, 2, 3, 0, 1, 2);
}

bool Matrix4x4::TryInvert(Matrix4x4& inverse) const
{
    std::array<std::array<float, 8>, 4> augmented{};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            augmented[row][column] = GetElement(row, column);
            augmented[row][column + 4] = row == column ? 1.0f : 0.0f;
        }
    }

    for (std::size_t pivotColumn = 0; pivotColumn < 4; ++pivotColumn)
    {
        std::size_t pivotRow = pivotColumn;
        for (std::size_t row = pivotColumn + 1; row < 4; ++row)
        {
            if (std::abs(augmented[row][pivotColumn]) > std::abs(augmented[pivotRow][pivotColumn]))
            {
                pivotRow = row;
            }
        }
        if (std::abs(augmented[pivotRow][pivotColumn]) <= InversionEpsilon)
        {
            return false;
        }
        std::swap(augmented[pivotColumn], augmented[pivotRow]);

        const float pivot = augmented[pivotColumn][pivotColumn];
        for (float& element : augmented[pivotColumn])
        {
            element /= pivot;
        }
        for (std::size_t row = 0; row < 4; ++row)
        {
            if (row == pivotColumn)
            {
                continue;
            }
            const float factor = augmented[row][pivotColumn];
            for (std::size_t column = 0; column < 8; ++column)
            {
                augmented[row][column] -= factor * augmented[pivotColumn][column];
            }
        }
    }

    std::array<float, 16> elements{};
    for (std::size_t row = 0; row < 4; ++row)
    for (std::size_t column = 0; column < 4; ++column)
    {
        elements[row * 4 + column] = augmented[row][column + 4];
    }
    inverse = Matrix4x4(elements);
    return true;
}

}
