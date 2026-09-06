#pragma once

#include <chrono>

namespace GameEngine::App
{

/// <summary>
/// 전체 시간과 프레임 간 경과 시간을 측정한다.
/// 표준 단조 시계 std::chrono::steady_clock을 사용하므로 App이 플랫폼 타이머 API에 의존하지 않는다.
/// </summary>
class GameTimer final
{
public:
    GameTimer();

    /// <summary>일시 정지 시간을 제외한 누적 실행 시간을 반환한다.</summary>
    /// <returns>초 단위 누적 실행 시간이다.</returns>
    [[nodiscard]] float GetTotalTime() const;

    /// <summary>마지막 Tick 사이의 경과 시간을 반환한다.</summary>
    /// <returns>초 단위 프레임 경과 시간이다.</returns>
    [[nodiscard]] float GetDeltaTime() const;

    /// <summary>메시지 루프 시작 전에 모든 시간 기준을 현재 시점으로 초기화한다.</summary>
    void Reset();

    /// <summary>정지된 타이머의 측정을 재개한다.</summary>
    void Start();

    /// <summary>타이머 측정을 일시 정지한다.</summary>
    void Stop();

    /// <summary>현재 시각을 읽어 프레임 경과 시간을 갱신한다.</summary>
    void Tick();

private:
    using Clock = std::chrono::steady_clock;

    /// <summary>Reset 시점이다. 누적 실행 시간의 기준이 된다.</summary>
    Clock::time_point mBaseTime;
    /// <summary>정지된 동안 쌓인 시간이다. 누적 실행 시간에서 빠진다.</summary>
    Clock::duration mPausedTime{};
    /// <summary>마지막 Stop 시점이다. 정지 중일 때만 의미가 있다.</summary>
    Clock::time_point mStopTime;
    /// <summary>마지막 Tick 시점이다. 프레임 경과 시간의 기준이 된다.</summary>
    Clock::time_point mPreviousTime;
    float mDeltaTime = 0.0f;
    bool mStopped = false;
};

}
