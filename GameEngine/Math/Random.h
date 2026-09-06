#pragma once

#include <cstdint>

#include "Vector.h"

namespace GameEngine::Math
{

/// <summary>
/// 결정적 의사 난수 생성기다.
///
/// 표준 라이브러리의 분포 클래스가 아니라 자기 알고리즘(xoshiro256**)으로 뽑는 이유는
/// 재현성이다: 같은 시드는 어느 플랫폼·컴파일러에서도 같은 수열을 내야 리플레이와 테스트가
/// 성립하는데, `std::uniform_real_distribution`은 구현마다 결과가 다르다.
///
/// 인스턴스가 상태를 갖는다 — 시스템마다 자기 생성기를 두면 한 곳의 소비가 다른 곳의 수열을
/// 흔들지 않는다. 편의를 위한 공용 인스턴스는 <see cref="Global"/>이다.
/// </summary>
class Random final
{
public:
    /// <summary>시드에서 만든다. 같은 시드는 같은 수열이다.</summary>
    explicit Random(std::uint64_t seed);

    /// <summary>시스템 엔트로피로 시드를 골라 만든다. 실행마다 다르다.</summary>
    [[nodiscard]] static Random FromEntropy();

    /// <summary>
    /// 공용 생성기다. 프로세스 시작 시 엔트로피로 시드되며, <see cref="Seed"/>로 다시 시드할
    /// 수 있다. 자기 생성기를 둘 이유가 없는 짧은 코드가 쓴다.
    /// </summary>
    [[nodiscard]] static Random& Global();

    /// <summary>이 생성기를 다시 시드한다. 수열이 처음부터 다시 시작된다.</summary>
    void Seed(std::uint64_t seed);
    [[nodiscard]] std::uint64_t GetSeed() const { return mSeed; }

    [[nodiscard]] std::uint32_t NextUInt32();
    [[nodiscard]] std::uint64_t NextUInt64();

    /// <summary>[0, 1)의 실수다. 24비트 정밀도라 1.0은 결코 나오지 않는다.</summary>
    [[nodiscard]] float NextFloat();

    /// <summary>[minimum, maximum)의 실수다.</summary>
    [[nodiscard]] float Range(float minimum, float maximum);

    /// <summary>[minimum, maximumExclusive)의 정수다. 범위가 비었으면 minimum이다.</summary>
    [[nodiscard]] int Range(int minimum, int maximumExclusive);

    /// <summary>주어진 확률로 true다.</summary>
    [[nodiscard]] bool NextBool(float probabilityOfTrue = 0.5f);

    /// <summary>단위 원 안의 점이다. 면적에 대해 고르다.</summary>
    [[nodiscard]] Vector2 InsideUnitCircle();

    /// <summary>단위 원 위의 점이다.</summary>
    [[nodiscard]] Vector2 OnUnitCircle();

    /// <summary>단위 구 위의 점이다. 방향이 고르다.</summary>
    [[nodiscard]] Vector3 OnUnitSphere();

    /// <summary>단위 구 안의 점이다. 부피에 대해 고르다.</summary>
    [[nodiscard]] Vector3 InsideUnitSphere();

private:
    std::uint64_t mSeed = 0;
    std::uint64_t mState[4] = {};
};

}
