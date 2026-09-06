#include "pch.h"
#include "Random.h"

#include <chrono>
#include <cmath>
#include <random>

#include "MathUtility.h"

namespace GameEngine::Math
{

namespace
{
    /// <summary>splitmix64. 시드 하나를 xoshiro의 상태 넷으로 펼치는 표준 방법이다.</summary>
    [[nodiscard]] std::uint64_t SplitMix64(std::uint64_t& state)
    {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    [[nodiscard]] constexpr std::uint64_t RotateLeft(const std::uint64_t value, const int bits)
    {
        return (value << bits) | (value >> (64 - bits));
    }
}

Random::Random(const std::uint64_t seed)
{
    Seed(seed);
}

Random Random::FromEntropy()
{
    std::random_device device;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::uint64_t entropy =
        (static_cast<std::uint64_t>(device()) << 32) ^ static_cast<std::uint64_t>(device());
    return Random(entropy ^ now);
}

Random& Random::Global()
{
    static Random global = FromEntropy();
    return global;
}

void Random::Seed(const std::uint64_t seed)
{
    mSeed = seed;
    std::uint64_t mixer = seed;
    for (std::uint64_t& word : mState)
    {
        word = SplitMix64(mixer);
    }
}

std::uint64_t Random::NextUInt64()
{
    // xoshiro256**
    const std::uint64_t result = RotateLeft(mState[1] * 5u, 7) * 9u;
    const std::uint64_t t = mState[1] << 17;
    mState[2] ^= mState[0];
    mState[3] ^= mState[1];
    mState[1] ^= mState[2];
    mState[0] ^= mState[3];
    mState[2] ^= t;
    mState[3] = RotateLeft(mState[3], 45);
    return result;
}

std::uint32_t Random::NextUInt32()
{
    return static_cast<std::uint32_t>(NextUInt64() >> 32);
}

float Random::NextFloat()
{
    // 상위 24비트 → [0, 1). float의 가수가 24비트라 모든 값이 정확히 표현된다.
    return static_cast<float>(NextUInt64() >> 40) * (1.0f / 16777216.0f);
}

float Random::Range(const float minimum, const float maximum)
{
    return minimum + (maximum - minimum) * NextFloat();
}

int Random::Range(const int minimum, const int maximumExclusive)
{
    if (maximumExclusive <= minimum)
    {
        return minimum;
    }
    const auto span = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(maximumExclusive) - static_cast<std::int64_t>(minimum));
    // 거부 샘플링으로 나머지 편향을 없앤다. 범위가 2^64에 가깝지 않은 한 거부는 드물다.
    const std::uint64_t limit = (~std::uint64_t{ 0 }) - ((~std::uint64_t{ 0 }) % span);
    std::uint64_t value = NextUInt64();
    while (value >= limit)
    {
        value = NextUInt64();
    }
    return static_cast<int>(static_cast<std::int64_t>(minimum) + static_cast<std::int64_t>(value % span));
}

bool Random::NextBool(const float probabilityOfTrue)
{
    return NextFloat() < probabilityOfTrue;
}

Vector2 Random::InsideUnitCircle()
{
    // 반지름을 sqrt로 뽑아야 면적에 대해 고르다.
    const float angle = NextFloat() * TwoPi;
    const float radius = std::sqrt(NextFloat());
    return { std::cos(angle) * radius, std::sin(angle) * radius };
}

Vector2 Random::OnUnitCircle()
{
    const float angle = NextFloat() * TwoPi;
    return { std::cos(angle), std::sin(angle) };
}

Vector3 Random::OnUnitSphere()
{
    // z를 고르게 뽑고 그 높이의 원 위에서 각도를 뽑으면 구면에 대해 고르다(아르키메데스).
    const float z = Range(-1.0f, 1.0f);
    const float angle = NextFloat() * TwoPi;
    const float ring = std::sqrt((std::max)(0.0f, 1.0f - z * z));
    return { std::cos(angle) * ring, std::sin(angle) * ring, z };
}

Vector3 Random::InsideUnitSphere()
{
    return OnUnitSphere() * std::cbrt(NextFloat());
}

}
