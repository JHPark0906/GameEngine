#include "RenderThreadTests.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "App/RenderThread.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderFrameBuilder.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using Clock = std::chrono::steady_clock;
    using GameEngine::App::RenderThread;

    /// <summary>한 구간이다. 두 스레드의 구간이 겹쳤는지를 이것으로 답한다.</summary>
    struct Interval
    {
        Clock::time_point begin;
        Clock::time_point end;
    };

    /// <summary>
    /// 주어진 시간이 지날 때까지 돈다.
    ///
    /// 재는 자리에서 <c>sleep_for</c>를 쓰지 않는 이유는 Windows의 기본 타이머 단위가 약 15
    /// 밀리초여서다: 8밀리초를 자라고 하면 15밀리초를 잔다. 그러면 「갱신 8, 렌더 16」이라고
    /// 적어 둔 모형이 실제로는 다른 모형이 되고, 거기서 나온 비율은 아무것도 말하지 않는다.
    /// </summary>
    void BusyWait(const Clock::duration duration)
    {
        const Clock::time_point end = Clock::now() + duration;
        while (Clock::now() < end)
        {
        }
    }

    /// <summary>두 구간이 함께 있던 시간이다. 겹치지 않았으면 0이다.</summary>
    [[nodiscard]] Clock::duration Overlap(const Interval& left, const Interval& right)
    {
        const Clock::time_point begin = (std::max)(left.begin, right.begin);
        const Clock::time_point end = (std::min)(left.end, right.end);
        return end > begin ? end - begin : Clock::duration::zero();
    }

    /// <summary>
    /// 아무것도 그리지 않고 자기가 언제 무엇을 했는지만 적는 장치다. 겹침은 화면이 아니라 시간의
    /// 성질이므로, 진짜 백엔드 없이 잴 수 있다.
    /// </summary>
    class RecordingDevice final : public GameEngine::Rendering::IGraphicsDevice
    {
    public:
        explicit RecordingDevice(const Clock::duration work) : mWork(work) {}

        bool Initialize(const GameEngine::Platform::NativeSurface&) override { return true; }

        /// <summary>장치 안에 있는 동안 사용자 수를 세는 자리다.</summary>
        class Entered final
        {
        public:
            explicit Entered(RecordingDevice& device) : mDevice(device)
            {
                const std::size_t users = mDevice.mUsers.fetch_add(1, std::memory_order_acq_rel) + 1;
                std::size_t peak = mDevice.mPeakUsers.load(std::memory_order_acquire);
                while (users > peak &&
                    !mDevice.mPeakUsers.compare_exchange_weak(peak, users, std::memory_order_acq_rel))
                {
                }
            }
            ~Entered() { mDevice.mUsers.fetch_sub(1, std::memory_order_acq_rel); }
            Entered(const Entered&) = delete;
            Entered& operator=(const Entered&) = delete;

        private:
            RecordingDevice& mDevice;
        };

        bool BeginFrame() override
        {
            mCurrentBegin = Clock::now();
            mFrameUsage = std::make_unique<Entered>(*this);
            return true;
        }

        GameEngine::Rendering::RenderTargetSize GetRenderTargetSize() const override
        {
            return { 640, 480 };
        }

        GameEngine::Rendering::GraphicsDeviceCapabilities GetCapabilities() const override
        {
            return {};
        }

        bool Render(const GameEngine::Rendering::RenderFrame& frame) override
        {
            // 프레임 번호를 폭에 실어 보냈다. 도착 순서를 그대로 적는다.
            std::lock_guard lock(mMutex);
            mFrameOrder.push_back(frame.GetRenderTargetSize().width);
            mCallOrder.push_back("frame:" + std::to_string(frame.GetRenderTargetSize().width));
            return true;
        }

        bool EndFrame() override
        {
            // present가 vsync까지 무는 것을 이 대기가 대신한다. 게임 스레드가 그동안 계속
            // 일한다는 것이 이 단위의 주장이다.
            BusyWait(mWork);
            {
                std::lock_guard lock(mMutex);
                mSubmissions.push_back({ mCurrentBegin, Clock::now() });
                mCallsAfterStop += mStopped.load(std::memory_order_acquire) ? 1 : 0;
            }
            const bool accepted = !mFailAfterFirst || mSubmissions.size() <= 1;
            // 장치를 놓는 것은 present가 끝나는 이 자리다.
            mFrameUsage.reset();
            return accepted;
        }

        bool RenderToImage(
            const GameEngine::Rendering::RenderFrame& frame,
            GameEngine::Rendering::CapturedImage& image,
            const CaptureRequest& request) override
        {
            const Entered entered(*this);
            {
                std::lock_guard lock(mMutex);
                // 무엇이 언제 일어났는지를 한 목록에 적는다: 캡처가 주 프레임보다 먼저인지는
                // 두 목록을 견주는 것이 아니라 이 순서를 읽는 것으로 답해야 한다.
                mCallOrder.push_back("capture:" + std::to_string(request.channel));
                mCaptureRequestedDeferred =
                    mCaptureRequestedDeferred || request.deferred;
            }
            // 프레임 번호를 폭에 실어 보냈으므로, 픽셀 한 칸에 그것을 담아 되돌린다.
            image.width = 1;
            image.height = 1;
            image.pixels.assign(
                GameEngine::Rendering::CapturedImage::BytesPerPixel,
                static_cast<std::byte>(frame.GetRenderTargetSize().width));
            return true;
        }

        /// <summary>
        /// 지금 이 장치 안에 있는 스레드의 수다. 1을 넘은 적이 있으면 두 스레드가 같은 장치를
        /// 동시에 만진 것이고, 진짜 백엔드였다면 그것이 크래시다.
        /// </summary>
        [[nodiscard]] std::size_t GetPeakConcurrentUsers() const
        {
            return mPeakUsers.load(std::memory_order_acquire);
        }

        /// <summary>게임 스레드가 캡처를 위해 장치를 직접 부르는 것을 흉내 낸다.</summary>
        void UseFromCaller(const std::chrono::milliseconds work)
        {
            const Entered entered(*this);
            BusyWait(work);
        }

        void FailAfterFirstFrame() { mFailAfterFirst = true; }
        void MarkStopped() { mStopped.store(true, std::memory_order_release); }

        [[nodiscard]] std::vector<Interval> GetSubmissions() const
        {
            std::lock_guard lock(mMutex);
            return mSubmissions;
        }

        [[nodiscard]] std::vector<unsigned int> GetFrameOrder() const
        {
            std::lock_guard lock(mMutex);
            return mFrameOrder;
        }

        [[nodiscard]] std::size_t GetCallsAfterStop() const
        {
            std::lock_guard lock(mMutex);
            return mCallsAfterStop;
        }

        /// <summary>장치가 불린 차례다. "capture:채널"과 "frame:번호"가 섞여 있다.</summary>
        [[nodiscard]] std::vector<std::string> GetCallOrder() const
        {
            std::lock_guard lock(mMutex);
            return mCallOrder;
        }

        [[nodiscard]] bool WasAnyCaptureDeferred() const
        {
            std::lock_guard lock(mMutex);
            return mCaptureRequestedDeferred;
        }

    private:
        Clock::duration mWork;
        Clock::time_point mCurrentBegin;
        mutable std::mutex mMutex;
        std::vector<Interval> mSubmissions;
        std::vector<unsigned int> mFrameOrder;
        std::vector<std::string> mCallOrder;
        bool mCaptureRequestedDeferred = false;
        std::size_t mCallsAfterStop = 0;
        bool mFailAfterFirst = false;
        std::atomic<bool> mStopped{ false };
        std::unique_ptr<Entered> mFrameUsage;
        std::atomic<std::size_t> mUsers{ 0 };
        std::atomic<std::size_t> mPeakUsers{ 0 };
    };

    /// <summary>프레임 번호를 실은 최소한의 프레임이다. 장치가 그 번호로 순서를 적는다.</summary>
    [[nodiscard]] GameEngine::Rendering::RenderFrame MakeNumberedFrame(const unsigned int number)
    {
        GameEngine::Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ number, 1 });
        return std::move(builder).Build();
    }
}

bool RunRenderThreadOverlapTests()
{
    constexpr auto DeviceWork = std::chrono::milliseconds(20);
    constexpr int FrameCount = 6;

    RecordingDevice device(DeviceWork);
    RenderThread renderThread(device);
    renderThread.Start();

    std::vector<Interval> gameWork;
    for (int frame = 0; frame < FrameCount; ++frame)
    {
        if (!renderThread.Submit({ {}, MakeNumberedFrame(static_cast<unsigned int>(frame)) }))
        {
            return Expect(false, "the render thread should accept every submitted frame");
        }
        // 제출 뒤 게임 스레드가 하는 일이다. 렌더 쪽이 앞 프레임의 present를 무는 동안 이것이
        // 돌아야 겹친 것이다.
        const Clock::time_point begin = Clock::now();
        std::this_thread::sleep_for(DeviceWork / 2);
        gameWork.push_back({ begin, Clock::now() });
    }
    renderThread.Stop();

    const std::vector<Interval> submissions = device.GetSubmissions();
    Clock::duration overlapped = Clock::duration::zero();
    for (const Interval& submission : submissions)
    {
        for (const Interval& work : gameWork)
        {
            overlapped += Overlap(submission, work);
        }
    }

    const std::vector<unsigned int> order = device.GetFrameOrder();
    bool inOrder = true;
    for (std::size_t index = 1; index < order.size(); ++index)
    {
        inOrder = inOrder && order[index] == order[index - 1] + 1;
    }

    return Expect(
               submissions.size() >= 2,
               "the render thread should have submitted the frames it was given") &&
        Expect(
            overlapped > Clock::duration::zero(),
            "the game thread's work and the render thread's submission should overlap in time") &&
        Expect(inOrder, "frames should reach the device in the order they were submitted");
}

bool RunRenderThreadQueueDepthTests()
{
    // 장치를 느리게 만들어 두면, 자리가 하나뿐이라는 것이 제출이 블록하는 것으로 나타난다.
    constexpr auto DeviceWork = std::chrono::milliseconds(40);
    RecordingDevice device(DeviceWork);
    RenderThread renderThread(device);
    renderThread.Start();

    // 첫 프레임은 곧바로 소비되고, 두 번째는 대기 자리를 채운다. 세 번째는 자리가 빌 때까지
    // 기다려야 하므로, 그 제출에 걸린 시간이 깊이가 1이라는 증거다.
    static_cast<void>(renderThread.Submit({ {}, MakeNumberedFrame(0) }));
    static_cast<void>(renderThread.Submit({ {}, MakeNumberedFrame(1) }));
    const Clock::time_point beforeThird = Clock::now();
    static_cast<void>(renderThread.Submit({ {}, MakeNumberedFrame(2) }));
    const Clock::duration waited = Clock::now() - beforeThird;
    renderThread.Stop();

    return Expect(
        waited > DeviceWork / 4,
        "a third frame should wait for the slot, because only one frame may be pending");
}

bool RunRenderThreadShutdownOrderTests()
{
    RecordingDevice device(std::chrono::milliseconds(5));
    {
        RenderThread renderThread(device);
        renderThread.Start();
        for (unsigned int frame = 0; frame < 3; ++frame)
        {
            static_cast<void>(renderThread.Submit({ {}, MakeNumberedFrame(frame) }));
        }
        renderThread.Stop();
        device.MarkStopped();
    }
    // 멈춘 뒤로 시간을 좀 준다. 스레드가 아직 살아 있었다면 여기서 장치를 한 번 더 불렀을 것이다.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    return Expect(
        device.GetCallsAfterStop() == 0,
        "no device call may happen after Stop returns, since the device is destroyed next");
}

bool RunRenderThreadFailureTests()
{
    RecordingDevice device(std::chrono::milliseconds(1));
    device.FailAfterFirstFrame();
    RenderThread renderThread(device);
    renderThread.Start();

    // 실패가 건너올 때까지 제출한다. 첫 실패 뒤로는 제출이 거절되는 것이 계약이다.
    bool sawRejection = false;
    for (int attempt = 0; attempt < 50 && !sawRejection; ++attempt)
    {
        if (!renderThread.Submit({ {}, MakeNumberedFrame(static_cast<unsigned int>(attempt)) }))
        {
            sawRejection = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const bool reported = renderThread.HasFailed();
    const char* const reason = renderThread.GetFailureReason();
    renderThread.Stop();

    return Expect(reported, "a failed present should be reported to the game thread") &&
        Expect(reason != nullptr, "the failure should name what went wrong") &&
        Expect(sawRejection, "a failed render thread should stop accepting frames");
}

bool RunRenderThreadDeviceHandoverTests()
{
    // 프레임 제출 직후 게임 스레드에서 같은 장치로 캡처를 요청하는 경합을 흉내 낸다.
    constexpr auto DeviceWork = std::chrono::milliseconds(15);
    RecordingDevice device(DeviceWork);
    RenderThread renderThread(device);
    renderThread.Start();

    for (unsigned int frame = 0; frame < 8; ++frame)
    {
        if (!renderThread.Submit({ {}, MakeNumberedFrame(frame) }))
        {
            return Expect(false, "the render thread should accept every submitted frame");
        }
        // 이것이 계약이다: 장치를 직접 부르기 전에 렌더 쪽이 손을 뗄 때까지 기다린다.
        renderThread.WaitUntilIdle();
        device.UseFromCaller(std::chrono::milliseconds(5));
    }
    renderThread.Stop();

    // 두 번째 국면은 그 기다림의 값이 어디까지인지를 고정한다: 캡처가 없는 실행 — 플레이어 —
    // 은 아무것도 기다리지 않고 겹침을 그대로 유지한다. 위와 같은 장치, 같은 제출, 다른 것은
    // 호출자가 장치를 부르지 않는다는 것뿐이다. 이 둘이 한 시험에 있어야 「고쳤다」가
    // 「모두가 느려졌다」로 바뀌는 것을 다음 사람이 알아챈다.
    RecordingDevice playerDevice(DeviceWork);
    RenderThread playerThread(playerDevice);
    playerThread.Start();

    std::vector<Interval> gameWork;
    for (unsigned int frame = 0; frame < 8; ++frame)
    {
        if (!playerThread.Submit({ {}, MakeNumberedFrame(frame) }))
        {
            return Expect(false, "the render thread should accept every submitted frame");
        }
        const Clock::time_point begin = Clock::now();
        std::this_thread::sleep_for(DeviceWork / 2);
        gameWork.push_back({ begin, Clock::now() });
    }
    playerThread.Stop();

    Clock::duration overlapped = Clock::duration::zero();
    for (const Interval& submission : playerDevice.GetSubmissions())
    {
        for (const Interval& work : gameWork)
        {
            overlapped += Overlap(submission, work);
        }
    }

    return Expect(
               device.GetPeakConcurrentUsers() <= 1,
               "the game thread and the render thread must never be inside the device at once") &&
        Expect(
            playerDevice.GetPeakConcurrentUsers() <= 1,
            "a run that never calls the device from the caller cannot overlap inside it either") &&
        Expect(
            overlapped > Clock::duration::zero(),
            "a run with no capture views should wait for nothing and keep its overlap");
}

bool RunRenderThreadCaptureTests()
{
    using GameEngine::App::CaptureJob;
    using GameEngine::App::CapturedView;

    constexpr unsigned int GameViewChannel = 0;
    constexpr unsigned int SceneViewChannel = 1;
    constexpr int FrameCount = 4;

    RecordingDevice device(std::chrono::milliseconds(2));
    RenderThread renderThread(device);
    renderThread.Start();

    // 에디터의 프레임을 흉내 낸다: 뷰 둘을 실어 제출하고, 다음 프레임 머리에서 픽셀을 가져간다.
    std::vector<CapturedView> received;
    std::vector<unsigned int> deliveredChannels;
    std::vector<unsigned int> deliveredFrames;
    for (int frame = 0; frame < FrameCount; ++frame)
    {
        renderThread.TakeCapturedViews(received);
        for (const CapturedView& view : received)
        {
            deliveredChannels.push_back(view.channel);
            // 장치가 프레임 번호를 픽셀에 실어 되돌렸다. 어느 프레임의 그림인지 이것으로 안다.
            deliveredFrames.push_back(
                view.image.pixels.empty()
                    ? 0u
                    : static_cast<unsigned int>(view.image.pixels.front()));
        }

        const auto number = static_cast<unsigned int>(frame);
        std::vector<CaptureJob> captures;
        captures.push_back({ MakeNumberedFrame(number), GameViewChannel });
        captures.push_back({ MakeNumberedFrame(number), SceneViewChannel });
        if (!renderThread.Submit({ std::move(captures), MakeNumberedFrame(number) }))
        {
            return Expect(false, "the render thread should accept every submitted frame");
        }
    }
    renderThread.Stop();
    // 멈춘 뒤 남아 있는 것까지 받는다.
    renderThread.TakeCapturedViews(received);
    for (const CapturedView& view : received)
    {
        deliveredChannels.push_back(view.channel);
        deliveredFrames.push_back(
            view.image.pixels.empty() ? 0u : static_cast<unsigned int>(view.image.pixels.front()));
    }

    // 캡처가 주 프레임보다 먼저 처리되는지는 장치가 불린 차례를 읽어 답한다. 한 프레임의 기록은
    // capture, capture, frame 순이어야 한다.
    const std::vector<std::string> order = device.GetCallOrder();
    bool capturesComeFirst = order.size() >= 3;
    for (std::size_t index = 0; index + 2 < order.size(); index += 3)
    {
        capturesComeFirst = capturesComeFirst && order[index].starts_with("capture:") &&
            order[index + 1].starts_with("capture:") && order[index + 2].starts_with("frame:");
    }

    // 채널이 실려 돌아와야 어느 뷰의 픽셀인지 말할 수 있다. 뷰 둘을 냈으니 둘 다 돌아온다.
    const bool bothChannelsCameBack =
        std::ranges::find(deliveredChannels, GameViewChannel) != deliveredChannels.end() &&
        std::ranges::find(deliveredChannels, SceneViewChannel) != deliveredChannels.end();
    const bool everyChannelIsOneOfOurs =
        std::ranges::all_of(deliveredChannels, [](const unsigned int channel)
        {
            return channel == GameViewChannel || channel == SceneViewChannel;
        });

    // 지연은 한 프레임이다: 게임 스레드가 프레임 N의 머리에서 받는 것은 N-1의 그림이고, 절대로
    // 그보다 오래된 것이 아니다. 첫 프레임에는 받을 것이 없다.
    bool everyDeliveryIsOneFrameLate = deliveredFrames.size() >= 2;
    for (std::size_t index = 0; index < deliveredFrames.size(); ++index)
    {
        // index/2번째 전달은 프레임 (index/2)에서 일어났고, 그 그림은 그 앞 프레임의 것이다.
        const unsigned int receivedAtFrame = static_cast<unsigned int>(index / 2) + 1;
        everyDeliveryIsOneFrameLate =
            everyDeliveryIsOneFrameLate && deliveredFrames[index] + 1 == receivedAtFrame;
    }

    return Expect(
               capturesComeFirst,
               "captures must reach the device before the frame they were submitted with") &&
        Expect(bothChannelsCameBack, "each view's pixels should come back on its own channel") &&
        Expect(everyChannelIsOneOfOurs, "no channel should appear that was never asked for") &&
        Expect(
            everyDeliveryIsOneFrameLate,
            "a view's pixels should arrive one frame later, as they did when the game thread "
            "captured them itself") &&
        Expect(
            !device.WasAnyCaptureDeferred(),
            "capture on the render thread has no reason to defer; deferring existed to keep the "
            "game thread off the GPU wait");
}

static const TestSupport::Registration gRenderThreadOverlapTests{
    "AppTiming", "render thread overlap tests should pass", RunRenderThreadOverlapTests };

static const TestSupport::Registration gRenderThreadQueueDepthTests{
    "AppTiming", "render thread queue depth tests should pass", RunRenderThreadQueueDepthTests };

static const TestSupport::Registration gRenderThreadShutdownOrderTests{
    "AppTiming", "render thread shutdown order tests should pass", RunRenderThreadShutdownOrderTests };

static const TestSupport::Registration gRenderThreadFailureTests{
    "AppTiming", "render thread failure tests should pass", RunRenderThreadFailureTests };

static const TestSupport::Registration gRenderThreadDeviceHandoverTests{
    "AppTiming", "render thread device handover tests should pass", RunRenderThreadDeviceHandoverTests };

static const TestSupport::Registration gRenderThreadCaptureTests{
    "AppTiming", "render thread capture tests should pass", RunRenderThreadCaptureTests };
