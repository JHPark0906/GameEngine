#include "pch.h"
#include "RenderThread.h"

#include <cstddef>
#include <iterator>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "../Diagnostics/Debug.h"
#include "../Rendering/IGraphicsDevice.h"

namespace GameEngine::App
{

RenderThread::RenderThread(Rendering::IGraphicsDevice& device) : mDevice(device)
{
}

RenderThread::~RenderThread()
{
    Stop();
}

void RenderThread::Start()
{
    if (mThread.joinable())
    {
        return;
    }
    // 장치가 처음 말하는 크기를 스레드가 돌기 전에 실어 둔다. 그러지 않으면 첫 프레임을 만드는
    // 게임 스레드가 0 크기의 면 위에 배치하게 된다.
    const Rendering::RenderTargetSize size = mDevice.GetRenderTargetSize();
    mRenderTargetWidth.store(size.width, std::memory_order_release);
    mRenderTargetHeight.store(size.height, std::memory_order_release);
    mThread = std::thread(&RenderThread::Run, this);
}

bool RenderThread::Submit(FrameSubmission submission)
{
    std::unique_lock lock(mMutex);
    mSlotAvailable.wait(lock, [this]
    {
        return !mPendingFrame.has_value() || mStopRequested ||
            mFailed.load(std::memory_order_acquire);
    });
    if (mStopRequested || mFailed.load(std::memory_order_acquire))
    {
        return false;
    }
    mPendingFrame = std::move(submission);
    lock.unlock();
    mFrameAvailable.notify_one();
    return true;
}

void RenderThread::TakeCapturedViews(std::vector<CapturedView>& views)
{
    views.clear();
    std::lock_guard lock(mMutex);
    views.swap(mCapturedViews);
}

void RenderThread::WaitUntilIdle()
{
    std::unique_lock lock(mMutex);
    mSlotAvailable.wait(lock, [this]
    {
        return (!mPendingFrame.has_value() && !mFrameInFlight) || mStopRequested ||
            mFailed.load(std::memory_order_acquire);
    });
}

Rendering::RenderTargetSize RenderThread::GetRenderTargetSize() const
{
    return {
        mRenderTargetWidth.load(std::memory_order_acquire),
        mRenderTargetHeight.load(std::memory_order_acquire)
    };
}

void RenderThread::Stop()
{
    {
        std::lock_guard lock(mMutex);
        mStopRequested = true;
    }
    // 두 조건 변수를 모두 깨운다: 렌더 스레드는 프레임을 기다리고 있을 수 있고, 게임 스레드는
    // 자리를 기다리고 있을 수 있다.
    mFrameAvailable.notify_all();
    mSlotAvailable.notify_all();
    if (mThread.joinable())
    {
        mThread.join();
    }
}

void RenderThread::Run()
{
    while (true)
    {
        FrameSubmission submission;
        {
            std::unique_lock lock(mMutex);
            mFrameAvailable.wait(lock, [this]
            {
                return mPendingFrame.has_value() || mStopRequested;
            });
            // 멈추라는 요청은 대기 중인 프레임보다 우선한다. 종료 중에 한 프레임을 더 그리는
            // 것은 아무도 보지 못하고, 창이 이미 사라지는 중일 수 있다.
            if (mStopRequested)
            {
                return;
            }
            submission = std::move(*mPendingFrame);
            mPendingFrame.reset();
            // 자리는 비었지만 장치는 지금부터 이 스레드의 것이다. 그 구간을 표시해야
            // WaitUntilIdle이 「자리가 비었다」가 아니라 「장치가 비었다」를 기다린다.
            mFrameInFlight = true;
        }
        // 자리를 비웠다는 것을 잠금 밖에서 알린다. 여기서부터 게임 스레드는 다음 프레임을
        // 제출할 수 있고, 이 스레드는 방금 받은 프레임을 그린다 — 그 둘이 겹치는 구간이다.
        mSlotAvailable.notify_one();

        // 캡처가 먼저다. RenderToImage는 BeginFrame과 EndFrame 사이에 설 수 없다.
        // 여기서 완성한 픽셀은 게임 스레드가 다음 프레임 머리에서 가져가므로 한 프레임 늦게 보인다.
        //
        // GPU 완료 대기는 이 렌더 스레드가 담당한다. 배치는 모든 현재 이미지를 회수한 뒤 반환한다.
        std::vector<CapturedView> captured;
        captured.reserve(submission.captures.size());
        for (const CaptureJob& job : submission.captures)
        {
            captured.push_back({ job.channel, {} });
        }
        // 출력 배열을 완성한 뒤 참조를 만든다. 재할당으로 배치가 빌린 이미지가 이동하지 않아야 한다.
        std::vector<Rendering::IGraphicsDevice::CaptureBatchItem> captureItems;
        captureItems.reserve(submission.captures.size());
        for (std::size_t index = 0; index < submission.captures.size(); ++index)
        {
            captureItems.push_back(
                { submission.captures[index].frame, captured[index].image, captured[index].channel });
        }
        mDevice.RenderToImages(captureItems);
        std::size_t validCaptures = 0;
        for (std::size_t index = 0; index < captureItems.size(); ++index)
        {
            if (captureItems[index].succeeded && captured[index].image.IsValid())
            {
                if (validCaptures != index) captured[validCaptures] = std::move(captured[index]);
                ++validCaptures;
            }
        }
        captured.resize(validCaptures);
        if (!captured.empty())
        {
            std::lock_guard lock(mMutex);
            // 게임 스레드가 아직 안 가져간 것이 있으면 그 위에 쌓는다. 채널이 실려 있으므로
            // 받는 쪽은 어느 뷰의 것인지 알 수 있다.
            mCapturedViews.insert(
                mCapturedViews.end(),
                std::make_move_iterator(captured.begin()),
                std::make_move_iterator(captured.end()));
        }

        const char* failure = nullptr;
        if (!mDevice.BeginFrame())
        {
            failure = "The graphics device could not begin a frame";
        }
        else
        {
            if (!mDevice.Render(submission.frame))
            {
                // 거부된 프레임은 콘텐츠나 프론트엔드의 문제이지 장치 실패가 아니다. 보고하고
                // 그래도 present해서, 창이 낡은 이미지 대신 지워진 타깃을 보이게 한다.
                Diagnostics::Debug::LogError("The graphics device rejected this frame.");
            }
            if (!mDevice.EndFrame())
            {
                failure = "The graphics device could not present a frame";
            }
        }

        const Rendering::RenderTargetSize size = mDevice.GetRenderTargetSize();
        mRenderTargetWidth.store(size.width, std::memory_order_release);
        mRenderTargetHeight.store(size.height, std::memory_order_release);

        // 장치를 놓는다. 이 알림을 기다리던 게임 스레드가 이제 장치를 부를 수 있다.
        {
            std::lock_guard lock(mMutex);
            mFrameInFlight = false;
        }
        mSlotAvailable.notify_all();

        if (failure)
        {
            // 실패는 여기서 처리되지 않는다. 프로세스를 끝내는 일도, bootstrap에 알리는 일도
            // 게임 스레드의 것이라서, 이 스레드는 사실만 남기고 물러난다 — 그러지 않으면
            // 에디터의 상태가 두 스레드에서 만져진다.
            mFailureReason.store(failure, std::memory_order_release);
            mFailed.store(true, std::memory_order_release);
            mSlotAvailable.notify_all();
            return;
        }
    }
}

}
