#include "pch.h"
#include "FrameLimiter.h"

#include <chrono>
#include <thread>

namespace GameEngine::App
{

FrameLimiter::FrameLimiter(const float targetFrameRate)
    : mFramePeriod(std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(1.0 / targetFrameRate))),
      mNextFrameTime(std::chrono::steady_clock::now() + mFramePeriod)
{
}

void FrameLimiter::WaitForNextFrame()
{
    using Clock = std::chrono::steady_clock;

    const Clock::time_point now = Clock::now();
    if (now >= mNextFrameTime)
    {
        // The frame overran its budget. Schedule from now rather than keeping the debt.
        mNextFrameTime = now + mFramePeriod;
        return;
    }

    // The system sleep is only accurate to its scheduling quantum — commonly around a millisecond
    // and sometimes fifteen — so sleeping right up to the deadline overshoots it. Sleep to just
    // short of it, then yield across the remainder, which stays exact without burning a core for
    // the whole wait.
    constexpr std::chrono::milliseconds SleepSlack{ 2 };
    if (mNextFrameTime - now > SleepSlack)
    {
        std::this_thread::sleep_until(mNextFrameTime - SleepSlack);
    }
    while (Clock::now() < mNextFrameTime)
    {
        std::this_thread::yield();
    }
    mNextFrameTime += mFramePeriod;
}

}
