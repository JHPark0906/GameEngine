#pragma once

#include "Behaviour.h"

namespace GameEngine::Runtime
{

/// <summary>
/// 시간을 프레임 번호로 바꾼다. 애니메이션의 산술 전부가 여기 있고, 컴포넌트는 이것을 부를 뿐이라
/// 장치도 장면도 없이 검사할 수 있다.
///
/// 프레임은 흐른 시간에서 곧바로 계산한다 — 프레임마다 하나씩 세는 것이 아니라. 그래야 한 프레임이
/// 길게 걸려 여러 장을 건너뛰어야 할 때도 애니메이션이 시계와 어긋나지 않고, 같은 시각에는 언제나
/// 같은 장이 나온다.
/// </summary>
/// <param name="elapsedSeconds">클립이 시작한 뒤 흐른 시간이다. 음수면 첫 프레임이다.</param>
/// <param name="firstFrame">클립의 첫 프레임 번호다. 시트 안에서의 번호다.</param>
/// <param name="frameCount">
/// 클립이 담은 프레임 수다. 0 이하이면 시트 전체를 뜻한다: 몇 장인지는 에셋만 알므로 번호를
/// 감싸지 않고 늘려 돌려주며, 감싸기는 프레임 사각형을 고르는 쪽이 한다 — 그런 클립은 loop와
/// 무관하게 계속 돈다.
/// </param>
/// <param name="frameRate">초당 프레임 수다. 0 이하이면 클립이 멈춘 것으로 본다.</param>
/// <param name="loop">계속 반복할지 여부다. 거짓이면 편도는 마지막 장, 왕복은 돌아온 첫 장에 머문다.</param>
/// <param name="pingPong">양 끝을 중복하지 않고 왕복한다. 프레임 수가 양수일 때 적용된다.</param>
/// <returns>그 시각에 보여 줄 프레임 번호다.</returns>
[[nodiscard]] int SelectAnimationFrame(
    float elapsedSeconds, int firstFrame, int frameCount, float frameRate, bool loop,
    bool pingPong = false);

/// <summary>
/// 스프라이트 시트의 프레임들을 차례로 보여 주는 컴포넌트다. 같은 GameObject의
/// <see cref="SpriteRenderer"/>가 그리는 프레임 번호를 매 업데이트마다 고쳐 쓴다.
///
/// 시트를 어떻게 나눌지는 스프라이트 에셋의 사이드카가 말하고, 이 컴포넌트는 그중 어느 구간을
/// 어떤 속도로 돌지만 말한다 — 같은 시트로 걷기와 서 있기를 나눠 쓰는 방식이다.
/// </summary>
class SpriteAnimator final : public Behaviour
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>클립이 시작하는 프레임 번호다.</summary>
    [[nodiscard]] int GetFirstFrame() const { return mFirstFrame; }
    void SetFirstFrame(int firstFrame);

    /// <summary>
    /// 클립이 담은 프레임 수다. 0이면 스프라이트 시트가 가진 프레임 전부를 뜻하며, 그 수는
    /// 렌더링이 에셋에서 읽는다. 왕복 모드는 그 수로 시간 표본을 해석하며,
    /// 기존 편도 모드는 프레임 번호를 시트 범위로 감싸는 동작을 유지한다.
    /// </summary>
    [[nodiscard]] int GetFrameCount() const { return mFrameCount; }
    void SetFrameCount(int frameCount);

    /// <summary>초당 프레임 수다.</summary>
    [[nodiscard]] float GetFrameRate() const { return mFrameRate; }
    void SetFrameRate(float frameRate);

    /// <summary>클립의 편도 또는 왕복 주기를 반복할지 여부다.</summary>
    [[nodiscard]] bool IsLooping() const { return mLoop; }
    void SetLooping(const bool loop) { mLoop = loop; }

    /// <summary>양 끝 프레임을 중복하지 않고 왕복한다. loop가 false면 한 번 왕복하고 첫 장에 머문다.</summary>
    [[nodiscard]] bool IsPingPong() const { return mPingPong; }
    void SetPingPong(const bool pingPong) { mPingPong = pingPong; }

    /// <summary>재생 중인지 여부다. 멈추면 그 자리의 프레임에 머문다.</summary>
    [[nodiscard]] bool IsPlaying() const { return mPlaying; }
    void SetPlaying(const bool playing) { mPlaying = playing; }

    /// <summary>클립을 처음부터 다시 재생한다.</summary>
    void Restart();

    /// <summary>클립이 시작한 뒤 흐른 시간이다. 초 단위다.</summary>
    [[nodiscard]] float GetElapsedSeconds() const { return mElapsedSeconds; }

private:
    void UpdateBehaviour(float deltaTime) override;

    int mFirstFrame = 0;
    int mFrameCount = 0;
    float mFrameRate = 12.0f;
    bool mLoop = true;
    bool mPingPong = false;
    bool mPlaying = true;
    /// <summary>재생 시간이다. 장면에 저장되지 않는 런타임 상태다.</summary>
    float mElapsedSeconds = 0.0f;
};

}
