#pragma once

#include <cmath>
#include <numbers>

namespace GameEngine::Math
{

/// <summary>
/// 수학 모듈이 공유하는 상수와 작은 함수들이다. 각도 변환처럼 여기저기서 다시 적히기 쉬운 것을
/// 한 번만 적어, 에디터와 게임 코드가 3.14159…를 되풀이하지 않게 한다.
/// </summary>

inline constexpr float Pi = std::numbers::pi_v<float>;
inline constexpr float TwoPi = 2.0f * Pi;
inline constexpr float HalfPi = 0.5f * Pi;
inline constexpr float DegreesToRadians = Pi / 180.0f;
inline constexpr float RadiansToDegrees = 180.0f / Pi;

/// <summary>부동소수 비교에 쓰는 기본 허용 오차다.</summary>
inline constexpr float Epsilon = 0.000001f;

[[nodiscard]] constexpr float ToRadians(const float degrees) { return degrees * DegreesToRadians; }
[[nodiscard]] constexpr float ToDegrees(const float radians) { return radians * RadiansToDegrees; }

/// <summary>a와 b 사이를 t(0..1)로 보간한다. t는 잘리지 않는다 — 외삽이 필요한 곳도 있다.</summary>
[[nodiscard]] constexpr float Lerp(const float a, const float b, const float t)
{
    return a + (b - a) * t;
}

[[nodiscard]] constexpr float Clamp01(const float value)
{
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

[[nodiscard]] inline bool IsNearlyEqual(const float a, const float b, const float tolerance = Epsilon)
{
    return std::abs(a - b) <= tolerance;
}

[[nodiscard]] inline bool IsNearlyZero(const float value, const float tolerance = Epsilon)
{
    return std::abs(value) <= tolerance;
}

/// <summary>각도를 (-180, 180]으로 접는다. 오일러 각을 비교하거나 표시할 때 쓴다.</summary>
[[nodiscard]] inline float WrapDegrees(float degrees)
{
    degrees = std::fmod(degrees, 360.0f);
    if (degrees > 180.0f)
    {
        degrees -= 360.0f;
    }
    else if (degrees <= -180.0f)
    {
        degrees += 360.0f;
    }
    return degrees;
}

}
