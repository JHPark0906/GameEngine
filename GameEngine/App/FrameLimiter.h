#pragma once

#include <chrono>

namespace GameEngine::App
{

/// <summary>
/// 루프를 목표 프레임 속도에 붙잡아 둔다.
///
/// targetFrameRate를 vsync 위의 상한으로 적용한다. 목표보다 빠른 디스플레이에서는 추가로
/// 기다리고, 느린 디스플레이에서는 present가 먼저 막으므로 여기서 기다리지 않는다.
///
/// 플랫폼 타이머 대신 표준 시계와 sleep을 사용한다.
/// </summary>
class FrameLimiter final
{
public:
    /// <param name="targetFrameRate">루프가 넘지 말아야 할 초당 프레임 수이다.</param>
    explicit FrameLimiter(float targetFrameRate);

    /// <summary>
    /// 다음 프레임의 예정 시각까지 막고, 그다음 프레임을 예약한다. 프레임의 작업이 제출된 뒤
    /// 프레임당 한 번 호출된다.
    ///
    /// 예산보다 오래 걸린 프레임은 빚을 쌓는 대신 일정을 다시 잡는다: 따라잡으려고 다음
    /// 프레임들을 연달아 돌리는 것은 긴 프레임 하나를 서두른 프레임 여럿과 바꾸는 일이고, 그것이
    /// 감추려는 끊김보다 더 나빠 보인다.
    /// </summary>
    void WaitForNextFrame();

private:
    std::chrono::steady_clock::duration mFramePeriod{};
    std::chrono::steady_clock::time_point mNextFrameTime;
};

}
