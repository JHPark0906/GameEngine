#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

#include <vector>

#include "../Rendering/RenderFrame.h"
#include "RenderSubmission.h"

namespace GameEngine::Rendering
{
class IGraphicsDevice;
}

namespace GameEngine::App
{

/// <summary>
/// 완성된 프레임을 받아 그래픽 장치에 제출하는 스레드다.
///
/// 게임 스레드는 프레임을 만들어 <see cref="Submit"/>에 넘기고 곧바로 다음 프레임의 업데이트로
/// 돌아간다. 그동안 이 스레드가 <c>BeginFrame</c>·<c>Render</c>·<c>EndFrame</c>을 돈다.
/// <c>EndFrame</c>은 vsync까지 블록하므로, 겹침으로 얻는 것은 새 일을 병렬로 하는 것이 아니라
/// 이미 있던 그 대기를 게임 스레드에서 걷어내는 것이다.
///
/// 프레임을 값으로 받는 것이 이것을 안전하게 만든다: <c>RenderFrame</c>은 값과
/// <c>shared_ptr&lt;const T&gt;</c>만 담고 라이브 장면을 가리키지 않으므로(그 파일의 계약이며
/// 시험이 지킨다), 넘긴 뒤 게임 스레드가 장면을 바꿔도 이 스레드가 읽는 것은 변하지 않는다.
///
/// 장치를 소유하지 않는다. 소유자는 이 객체보다 오래 살아야 하며, 그 순서는
/// <see cref="Stop"/>이 세운다.
/// </summary>
class RenderThread final
{
public:
    /// <summary>제출을 받을 장치를 붙인다. 장치는 이 객체보다 오래 살아야 한다.</summary>
    explicit RenderThread(Rendering::IGraphicsDevice& device);

    /// <summary>스레드를 멈추고 합류한다. <see cref="Stop"/>과 같다.</summary>
    ~RenderThread();

    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;
    RenderThread(RenderThread&&) = delete;
    RenderThread& operator=(RenderThread&&) = delete;

    /// <summary>스레드를 띄운다. 두 번 불러도 하나만 돈다.</summary>
    void Start();

    /// <summary>
    /// 이 프레임의 할 일을 렌더 스레드에 넘긴다. 대기 중인 것이 이미 있으면 그것이 소비될
    /// 때까지 블록한다 — 그것이 큐 깊이 1의 뜻이다.
    ///
    /// 캡처가 있으면 <b>주 프레임보다 먼저</b> 그려진다. 그래야 그 픽셀이 이 패스가 끝날 때
    /// 준비되고, 게임 스레드가 다음 프레임 머리에서 가져간다. 뷰에는 한 프레임의 지연이 있다.
    /// </summary>
    /// <param name="submission">완성된 프레임과 캡처들이다. 호출자는 넘긴 뒤 쓰지 않는다.</param>
    /// <returns>받아들였으면 true, 렌더 쪽이 이미 실패했거나 멈추는 중이면 false다.</returns>
    [[nodiscard]] bool Submit(FrameSubmission submission);

    /// <summary>
    /// 렌더 스레드가 그려 둔 뷰 픽셀을 가져간다. 목록은 비워진 채 채워지고, 아직 없으면 비어
    /// 있다. 게임 스레드가 프레임 머리에서 부른다.
    /// </summary>
    void TakeCapturedViews(std::vector<CapturedView>& views);

    /// <summary>
    /// 렌더 스레드가 장치에서 손을 뗄 때까지 기다린다. 대기 중인 프레임도, 그리는 중인 프레임도
    /// 없는 상태가 될 때까지다.
    ///
    /// <b>게임 스레드가 장치를 직접 불러야 할 때는 반드시 이것을 먼저 부른다.</b> 그래픽 장치는
    /// 한 번에 한 스레드의 것이다 — D3D11의 즉시 컨텍스트도, D3D12의 명령 할당자도 동시에 두
    /// 스레드가 만지면 안 된다. 캡처 뷰를 이미지로 받는 길이 그런 경우이고, 그 호출이 이
    /// 기다림 없이 일어나면 두 스레드가 같은 장치를 동시에 만진다.
    /// </summary>
    void WaitUntilIdle();

    /// <summary>렌더 쪽이 프레임을 실패했는지다. 실패의 처리는 게임 스레드의 몫이다.</summary>
    [[nodiscard]] bool HasFailed() const { return mFailed.load(std::memory_order_acquire); }

    /// <summary>
    /// 무엇이 실패했는지다. 실패하지 않았으면 null이다. 리터럴만 담기므로 수명 문제가 없다.
    /// </summary>
    [[nodiscard]] const char* GetFailureReason() const
    {
        return mFailureReason.load(std::memory_order_acquire);
    }

    /// <summary>
    /// 이 스레드가 마지막으로 본 렌더 타깃 크기다.
    ///
    /// 장치는 이 스레드의 것이므로 게임 스레드가 직접 물을 수 없다. 대신 프레임마다 여기에
    /// 실린 값을 읽는다 — 창 크기 변경이 한 프레임 늦게 반영되며, 그것은 큐 깊이 1이 이미
    /// 뜻하는 지연과 같은 크기다.
    /// </summary>
    [[nodiscard]] Rendering::RenderTargetSize GetRenderTargetSize() const;

    /// <summary>
    /// 스레드를 멈추고 합류한다. 반환한 뒤에는 장치를 부르는 것이 아무도 없으므로, 장치를
    /// 파괴하기 전에 이것이 먼저 반환해야 한다. 여러 번 불러도 안전하다.
    /// </summary>
    void Stop();

private:
    void Run();

    Rendering::IGraphicsDevice& mDevice;

    mutable std::mutex mMutex;
    std::condition_variable mFrameAvailable;
    std::condition_variable mSlotAvailable;

    /// <summary>
    /// 대기 중인 프레임 하나. **깊이를 1보다 크게 하면 처리량은 늘지만 입력 지연이 그만큼
    /// 늘어난다** — 사람이 누른 것이 화면에 나타나기까지 프레임 하나가 더 끼기 때문이다. 이
    /// 큐는 입력 지연을 제한하기 위해 깊이 1을 유지한다. 깊이를 늘릴 때는 입력 지연도 측정한다.
    /// </summary>
    std::optional<FrameSubmission> mPendingFrame;

    /// <summary>그려 두고 게임 스레드가 가져가기를 기다리는 픽셀이다.</summary>
    std::vector<CapturedView> mCapturedViews;

    /// <summary>
    /// 렌더 스레드가 지금 장치를 쥐고 있는지다. 대기 자리가 비었다는 것과 다르다: 자리를 비운
    /// 직후부터 present가 끝날 때까지가 장치를 쥔 구간이고, 그 사이에 게임 스레드가 장치를
    /// 부르면 두 스레드가 겹친다.
    /// </summary>
    bool mFrameInFlight = false;

    bool mStopRequested = false;
    std::thread mThread;

    std::atomic<bool> mFailed{ false };
    std::atomic<const char*> mFailureReason{ nullptr };

    /// <summary>렌더 스레드가 발행하고 게임 스레드가 읽는 렌더 타깃 크기다.</summary>
    std::atomic<unsigned int> mRenderTargetWidth{ 0 };
    std::atomic<unsigned int> mRenderTargetHeight{ 0 };
};

}
