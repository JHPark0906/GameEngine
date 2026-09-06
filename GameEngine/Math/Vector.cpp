#include "pch.h"
#include "Vector.h"

#include <cmath>

#include "MathUtility.h"

namespace GameEngine::Math
{

const Vector2 Vector2::Zero{ 0.0f, 0.0f };
const Vector2 Vector2::One{ 1.0f, 1.0f };
const Vector2 Vector2::Right{ 1.0f, 0.0f };
const Vector2 Vector2::Up{ 0.0f, 1.0f };

const Vector2Int Vector2Int::Zero{ 0, 0 };
const Vector2Int Vector2Int::One{ 1, 1 };

const Vector3 Vector3::Zero{ 0.0f, 0.0f, 0.0f };
const Vector3 Vector3::One{ 1.0f, 1.0f, 1.0f };
const Vector3 Vector3::Right{ 1.0f, 0.0f, 0.0f };
const Vector3 Vector3::Up{ 0.0f, 1.0f, 0.0f };
const Vector3 Vector3::Forward{ 0.0f, 0.0f, 1.0f };

const Vector3Int Vector3Int::Zero{ 0, 0, 0 };
const Vector3Int Vector3Int::One{ 1, 1, 1 };

float Vector2::GetLength() const
{
    return std::sqrt(GetLengthSquared());
}

Vector2 Vector2::Normalized() const
{
    const float length = GetLength();
    return length <= Epsilon ? *this : *this / length;
}

float Vector3::GetLength() const
{
    return std::sqrt(GetLengthSquared());
}

Vector3 Vector3::Normalized() const
{
    const float length = GetLength();
    return length <= Epsilon ? *this : *this / length;
}

bool Vector3::IsFinite() const
{
    return std::isfinite(GetX()) && std::isfinite(GetY()) && std::isfinite(GetZ());
}

}
