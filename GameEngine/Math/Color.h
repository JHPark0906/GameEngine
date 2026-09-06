#pragma once

#include <cmath>

namespace GameEngine::Math
{

/// <summary>선형 RGBA 색상 값을 표현한다.</summary>
struct Color
{
public:
    constexpr Color() = default;
    constexpr Color(float red, float green, float blue, float alpha = 1.0f)
        : r(red), g(green), b(blue), a(alpha)
    {
    }

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    [[nodiscard]] constexpr bool operator==(const Color&) const = default;

    [[nodiscard]] bool IsFinite() const
    {
        return std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a);
    }

    static const Color Black;
    static const Color White;
    static const Color Clear;
};

}
