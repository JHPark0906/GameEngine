#include "pch.h"

#include "GameTimer.h"

#include <chrono>

namespace GameEngine::App
{

namespace
{
    [[nodiscard]] float ToSeconds(const std::chrono::steady_clock::duration duration)
    {
        return std::chrono::duration_cast<std::chrono::duration<float>>(duration).count();
    }
}

GameTimer::GameTimer()
{
    Reset();
}

float GameTimer::GetTotalTime() const
{
    // 정지 중이면 정지 시점까지, 아니면 지금까지다. 어느 쪽이든 정지된 동안 쌓인 시간은 실행
    // 시간이 아니므로 뺀다.
    const Clock::time_point end = mStopped ? mStopTime : Clock::now();
    return ToSeconds(end - mBaseTime - mPausedTime);
}

float GameTimer::GetDeltaTime() const
{
    return mDeltaTime;
}

void GameTimer::Reset()
{
    const Clock::time_point now = Clock::now();
    mBaseTime = now;
    mPreviousTime = now;
    mStopTime = now;
    mPausedTime = {};
    mDeltaTime = 0.0f;
    mStopped = false;
}

void GameTimer::Start()
{
    if (mStopped)
    {
        const Clock::time_point now = Clock::now();
        mPausedTime += now - mStopTime;
        mPreviousTime = now;
        mStopped = false;
    }
}

void GameTimer::Stop()
{
    if (!mStopped)
    {
        mStopTime = Clock::now();
        mStopped = true;
    }
}

void GameTimer::Tick()
{
    if (mStopped)
    {
        mDeltaTime = 0.0f;
        return;
    }

    const Clock::time_point now = Clock::now();
    // 단조 시계를 사용하므로 경과 시간은 음수가 되지 않는다.
    mDeltaTime = ToSeconds(now - mPreviousTime);
    mPreviousTime = now;
}

}
