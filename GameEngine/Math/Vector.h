#pragma once

#include <array>

namespace GameEngine::Math
{

/// <summary>
/// 두 개의 부동소수점 성분으로 구성된 벡터이다.
///
/// 산술 연산자는 성분별로 동작하고, 벡터끼리의 곱은 성분별 곱이다 — 내적과 외적은 이름으로
/// 부른다(Dot, Cross). 값 타입이라 모든 연산이 새 벡터를 돌려준다.
/// </summary>
struct Vector2
{
    constexpr Vector2() = default;
    /// <param name="x">X축 성분이다.</param>
    /// <param name="y">Y축 성분이다.</param>
    constexpr Vector2(const float x, const float y) : mElements{ x, y } {}

    [[nodiscard]] constexpr float GetX() const { return mElements[0]; }
    [[nodiscard]] constexpr float GetY() const { return mElements[1]; }

    [[nodiscard]] constexpr Vector2 operator+(const Vector2& other) const { return { GetX() + other.GetX(), GetY() + other.GetY() }; }
    [[nodiscard]] constexpr Vector2 operator-(const Vector2& other) const { return { GetX() - other.GetX(), GetY() - other.GetY() }; }
    [[nodiscard]] constexpr Vector2 operator*(const Vector2& other) const { return { GetX() * other.GetX(), GetY() * other.GetY() }; }
    [[nodiscard]] constexpr Vector2 operator*(const float scalar) const { return { GetX() * scalar, GetY() * scalar }; }
    [[nodiscard]] constexpr Vector2 operator/(const float scalar) const { return { GetX() / scalar, GetY() / scalar }; }
    [[nodiscard]] constexpr Vector2 operator-() const { return { -GetX(), -GetY() }; }
    constexpr Vector2& operator+=(const Vector2& other) { return *this = *this + other; }
    constexpr Vector2& operator-=(const Vector2& other) { return *this = *this - other; }
    constexpr Vector2& operator*=(const float scalar) { return *this = *this * scalar; }
    constexpr Vector2& operator/=(const float scalar) { return *this = *this / scalar; }
    [[nodiscard]] constexpr bool operator==(const Vector2&) const = default;

    [[nodiscard]] constexpr float Dot(const Vector2& other) const { return GetX() * other.GetX() + GetY() * other.GetY(); }
    [[nodiscard]] constexpr float GetLengthSquared() const { return Dot(*this); }
    [[nodiscard]] float GetLength() const;
    /// <summary>단위 길이 사본이다. 길이가 0인 벡터는 그대로 반환된다.</summary>
    [[nodiscard]] Vector2 Normalized() const;
    [[nodiscard]] float DistanceTo(const Vector2& other) const { return (other - *this).GetLength(); }
    [[nodiscard]] static constexpr Vector2 Lerp(const Vector2& a, const Vector2& b, const float t) { return a + (b - a) * t; }

    static const Vector2 Zero;
    static const Vector2 One;
    static const Vector2 Right;
    static const Vector2 Up;

private:
    std::array<float, 2> mElements{};
};

[[nodiscard]] constexpr Vector2 operator*(const float scalar, const Vector2& vector) { return vector * scalar; }

/// <summary>두 개의 정수 성분으로 구성된 벡터이다.</summary>
struct Vector2Int
{
    constexpr Vector2Int() = default;
    constexpr Vector2Int(const int x, const int y) : mElements{ x, y } {}

    [[nodiscard]] constexpr int GetX() const { return mElements[0]; }
    [[nodiscard]] constexpr int GetY() const { return mElements[1]; }

    [[nodiscard]] constexpr Vector2Int operator+(const Vector2Int& other) const { return { GetX() + other.GetX(), GetY() + other.GetY() }; }
    [[nodiscard]] constexpr Vector2Int operator-(const Vector2Int& other) const { return { GetX() - other.GetX(), GetY() - other.GetY() }; }
    [[nodiscard]] constexpr Vector2Int operator*(const int scalar) const { return { GetX() * scalar, GetY() * scalar }; }
    [[nodiscard]] constexpr Vector2Int operator-() const { return { -GetX(), -GetY() }; }
    constexpr Vector2Int& operator+=(const Vector2Int& other) { return *this = *this + other; }
    constexpr Vector2Int& operator-=(const Vector2Int& other) { return *this = *this - other; }
    [[nodiscard]] constexpr bool operator==(const Vector2Int&) const = default;

    /// <summary>같은 성분의 실수 벡터다.</summary>
    [[nodiscard]] constexpr Vector2 ToVector2() const { return { static_cast<float>(GetX()), static_cast<float>(GetY()) }; }

    static const Vector2Int Zero;
    static const Vector2Int One;

private:
    std::array<int, 2> mElements{};
};

/// <summary>
/// 세 개의 부동소수점 성분으로 구성된 벡터이다. 왼손 좌표계에서 +X가 오른쪽, +Y가 위, +Z가
/// 앞이며 Right/Up/Forward 상수가 그것을 이름 짓는다.
/// </summary>
struct Vector3
{
    constexpr Vector3() = default;
    constexpr Vector3(const float x, const float y, const float z) : mElements{ x, y, z } {}

    [[nodiscard]] constexpr float GetX() const { return mElements[0]; }
    [[nodiscard]] constexpr float GetY() const { return mElements[1]; }
    [[nodiscard]] constexpr float GetZ() const { return mElements[2]; }

    [[nodiscard]] constexpr Vector3 operator+(const Vector3& other) const { return { GetX() + other.GetX(), GetY() + other.GetY(), GetZ() + other.GetZ() }; }
    [[nodiscard]] constexpr Vector3 operator-(const Vector3& other) const { return { GetX() - other.GetX(), GetY() - other.GetY(), GetZ() - other.GetZ() }; }
    [[nodiscard]] constexpr Vector3 operator*(const Vector3& other) const { return { GetX() * other.GetX(), GetY() * other.GetY(), GetZ() * other.GetZ() }; }
    [[nodiscard]] constexpr Vector3 operator*(const float scalar) const { return { GetX() * scalar, GetY() * scalar, GetZ() * scalar }; }
    [[nodiscard]] constexpr Vector3 operator/(const float scalar) const { return { GetX() / scalar, GetY() / scalar, GetZ() / scalar }; }
    [[nodiscard]] constexpr Vector3 operator-() const { return { -GetX(), -GetY(), -GetZ() }; }
    constexpr Vector3& operator+=(const Vector3& other) { return *this = *this + other; }
    constexpr Vector3& operator-=(const Vector3& other) { return *this = *this - other; }
    constexpr Vector3& operator*=(const float scalar) { return *this = *this * scalar; }
    constexpr Vector3& operator/=(const float scalar) { return *this = *this / scalar; }
    [[nodiscard]] constexpr bool operator==(const Vector3&) const = default;

    [[nodiscard]] constexpr float Dot(const Vector3& other) const
    {
        return GetX() * other.GetX() + GetY() * other.GetY() + GetZ() * other.GetZ();
    }
    /// <summary>외적이다. 왼손 좌표계에서 Right × Up = Forward이다.</summary>
    [[nodiscard]] constexpr Vector3 Cross(const Vector3& other) const
    {
        return {
            GetY() * other.GetZ() - GetZ() * other.GetY(),
            GetZ() * other.GetX() - GetX() * other.GetZ(),
            GetX() * other.GetY() - GetY() * other.GetX(),
        };
    }
    [[nodiscard]] constexpr float GetLengthSquared() const { return Dot(*this); }
    [[nodiscard]] float GetLength() const;
    /// <summary>단위 길이 사본이다. 길이가 0인 벡터는 그대로 반환된다.</summary>
    [[nodiscard]] Vector3 Normalized() const;
    [[nodiscard]] float DistanceTo(const Vector3& other) const { return (other - *this).GetLength(); }
    [[nodiscard]] static constexpr Vector3 Lerp(const Vector3& a, const Vector3& b, const float t) { return a + (b - a) * t; }
    /// <summary>모든 성분이 유한한지이다. 렌더링에 넘기기 전의 검사에 쓴다.</summary>
    [[nodiscard]] bool IsFinite() const;

    static const Vector3 Zero;
    static const Vector3 One;
    static const Vector3 Right;
    static const Vector3 Up;
    static const Vector3 Forward;

private:
    std::array<float, 3> mElements{};
};

[[nodiscard]] constexpr Vector3 operator*(const float scalar, const Vector3& vector) { return vector * scalar; }

/// <summary>세 개의 정수 성분으로 구성된 벡터이다.</summary>
struct Vector3Int
{
    constexpr Vector3Int() = default;
    constexpr Vector3Int(const int x, const int y, const int z) : mElements{ x, y, z } {}

    [[nodiscard]] constexpr int GetX() const { return mElements[0]; }
    [[nodiscard]] constexpr int GetY() const { return mElements[1]; }
    [[nodiscard]] constexpr int GetZ() const { return mElements[2]; }

    [[nodiscard]] constexpr Vector3Int operator+(const Vector3Int& other) const { return { GetX() + other.GetX(), GetY() + other.GetY(), GetZ() + other.GetZ() }; }
    [[nodiscard]] constexpr Vector3Int operator-(const Vector3Int& other) const { return { GetX() - other.GetX(), GetY() - other.GetY(), GetZ() - other.GetZ() }; }
    [[nodiscard]] constexpr Vector3Int operator*(const int scalar) const { return { GetX() * scalar, GetY() * scalar, GetZ() * scalar }; }
    [[nodiscard]] constexpr Vector3Int operator-() const { return { -GetX(), -GetY(), -GetZ() }; }
    constexpr Vector3Int& operator+=(const Vector3Int& other) { return *this = *this + other; }
    constexpr Vector3Int& operator-=(const Vector3Int& other) { return *this = *this - other; }
    [[nodiscard]] constexpr bool operator==(const Vector3Int&) const = default;

    [[nodiscard]] constexpr Vector3 ToVector3() const
    {
        return { static_cast<float>(GetX()), static_cast<float>(GetY()), static_cast<float>(GetZ()) };
    }

    static const Vector3Int Zero;
    static const Vector3Int One;

private:
    std::array<int, 3> mElements{};
};

/// <summary>네 개의 부동소수점 성분으로 구성된 벡터이다.</summary>
struct Vector4
{
    constexpr Vector4() = default;
    constexpr Vector4(const float x, const float y, const float z, const float w) : mElements{ x, y, z, w } {}

    [[nodiscard]] constexpr float GetX() const { return mElements[0]; }
    [[nodiscard]] constexpr float GetY() const { return mElements[1]; }
    [[nodiscard]] constexpr float GetZ() const { return mElements[2]; }
    [[nodiscard]] constexpr float GetW() const { return mElements[3]; }

    [[nodiscard]] constexpr Vector4 operator+(const Vector4& other) const { return { GetX() + other.GetX(), GetY() + other.GetY(), GetZ() + other.GetZ(), GetW() + other.GetW() }; }
    [[nodiscard]] constexpr Vector4 operator-(const Vector4& other) const { return { GetX() - other.GetX(), GetY() - other.GetY(), GetZ() - other.GetZ(), GetW() - other.GetW() }; }
    [[nodiscard]] constexpr Vector4 operator*(const float scalar) const { return { GetX() * scalar, GetY() * scalar, GetZ() * scalar, GetW() * scalar }; }
    [[nodiscard]] constexpr Vector4 operator/(const float scalar) const { return { GetX() / scalar, GetY() / scalar, GetZ() / scalar, GetW() / scalar }; }
    [[nodiscard]] constexpr Vector4 operator-() const { return { -GetX(), -GetY(), -GetZ(), -GetW() }; }
    [[nodiscard]] constexpr bool operator==(const Vector4&) const = default;

    [[nodiscard]] constexpr float Dot(const Vector4& other) const
    {
        return GetX() * other.GetX() + GetY() * other.GetY() + GetZ() * other.GetZ() + GetW() * other.GetW();
    }

private:
    std::array<float, 4> mElements{};
};

/// <summary>네 개의 정수 성분으로 구성된 벡터이다.</summary>
struct Vector4Int
{
    constexpr Vector4Int() = default;
    constexpr Vector4Int(const int x, const int y, const int z, const int w) : mElements{ x, y, z, w } {}

    [[nodiscard]] constexpr int GetX() const { return mElements[0]; }
    [[nodiscard]] constexpr int GetY() const { return mElements[1]; }
    [[nodiscard]] constexpr int GetZ() const { return mElements[2]; }
    [[nodiscard]] constexpr int GetW() const { return mElements[3]; }
    [[nodiscard]] constexpr bool operator==(const Vector4Int&) const = default;

private:
    std::array<int, 4> mElements{};
};

}
