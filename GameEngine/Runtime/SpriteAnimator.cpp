#include "pch.h"
#include "SpriteAnimator.h"

#include "GameObject.h"
#include "PropertyDescriptor.h"
#include "SpriteRenderer.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// SpriteAnimator가 선언하는 속성들이다. 클립이 시트의 어느 구간을 어떤 속도로 도는지만
    /// 담는다 — 지금 몇 번째 프레임인지는 시간에서 계산되는 런타임 상태라 저장하지 않는다.
    /// </summary>
    std::span<const PropertyDescriptor> SpriteAnimatorProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<SpriteAnimator>(
                "firstFrame", "First Frame",
                &SpriteAnimator::GetFirstFrame, &SpriteAnimator::SetFirstFrame),
            MakeProperty<SpriteAnimator>(
                "frameCount", "Frame Count",
                &SpriteAnimator::GetFrameCount, &SpriteAnimator::SetFrameCount),
            MakeProperty<SpriteAnimator>(
                "frameRate", "Frame Rate",
                &SpriteAnimator::GetFrameRate, &SpriteAnimator::SetFrameRate),
            MakeProperty<SpriteAnimator>(
                "loop", "Loop", &SpriteAnimator::IsLooping, &SpriteAnimator::SetLooping),
            MakeProperty<SpriteAnimator>(
                "pingPong", "Ping Pong", &SpriteAnimator::IsPingPong, &SpriteAnimator::SetPingPong),
            MakeProperty<SpriteAnimator>(
                "playing", "Playing", &SpriteAnimator::IsPlaying, &SpriteAnimator::SetPlaying),
        };
        return properties;
    }
}

int SelectAnimationFrame(
    const float elapsedSeconds,
    const int firstFrame,
    const int frameCount,
    const float frameRate,
    const bool loop,
    const bool pingPong)
{
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0f ||
        !std::isfinite(frameRate) || frameRate <= 0.0f)
    {
        return firstFrame;
    }

    // 흐른 시간을 프레임 길이로 나눈 몫이 몇 장을 지났는지다. double로 나누는 이유는 오래 켜 둔
    // 장면 — 초가 수만이 되는 — 에서도 몫이 프레임 하나를 건너뛸 만큼 뭉개지지 않게 하려는 것이다.
    const double advanced =
        static_cast<double>(elapsedSeconds) * static_cast<double>(frameRate);

    // 프레임 수를 말하지 않은 클립은 시트 전체를 도는데, 시트가 몇 장인지는 에셋만 안다.
    // 그래서 번호를 감싸지 않고 늘려 보내고, 감싸기는 프레임 사각형을 고르는 쪽이 한다.
    // 그런 클립은 멈출 자리를 셀 수 없으므로 loop와 무관하게 계속 돈다.
    if (frameCount <= 0)
    {
        return firstFrame + static_cast<int>(advanced);
    }
    if (pingPong)
    {
        if (frameCount == 1)
        {
            return firstFrame;
        }
        const double end = static_cast<double>(frameCount - 1);
        // 끝점을 한 번씩만 쓰므로 N장이 왕복하는 주기는 2N-2다: 0,1,2,1,0,...
        const double period = 2.0 * end;
        const double step = std::floor(loop ? std::fmod(advanced, period)
                                           : (std::min)(advanced, period));
        return firstFrame + static_cast<int>(step <= end ? step : period - step);
    }
    if (!loop)
    {
        const double clamped = (std::min)(advanced, static_cast<double>(frameCount - 1));
        return firstFrame + static_cast<int>(clamped);
    }
    return firstFrame + static_cast<int>(std::fmod(advanced, static_cast<double>(frameCount)));
}

const ComponentType& SpriteAnimator::StaticType()
{
    static const ComponentType type{
        "SpriteAnimator", &Behaviour::StaticType(), &SpriteAnimatorProperties,
        &MakeComponentInstance<SpriteAnimator> };
    return type;
}

void SpriteAnimator::SetFirstFrame(const int firstFrame)
{
    mFirstFrame = (std::max)(firstFrame, 0);
}

void SpriteAnimator::SetFrameCount(const int frameCount)
{
    mFrameCount = (std::max)(frameCount, 0);
}

void SpriteAnimator::SetFrameRate(const float frameRate)
{
    mFrameRate = std::isfinite(frameRate) && frameRate > 0.0f ? frameRate : 0.0f;
}

void SpriteAnimator::Restart()
{
    mElapsedSeconds = 0.0f;
}

void SpriteAnimator::UpdateBehaviour(const float deltaTime)
{
    GameObject* const gameObject = GetGameObject();
    if (!gameObject)
    {
        return;
    }
    if (mPlaying && std::isfinite(deltaTime) && deltaTime > 0.0f)
    {
        mElapsedSeconds += deltaTime;
    }

    // 전체 시트의 왕복은 렌더러마다 에셋의 프레임 수가 달라질 수 있다. 시간을 복사해 두고
    // 에셋이 해결된 그리기 단계에서 번호를 확정한다. 그리기는 재생 시간을 진행시키지 않는다.
    for (SpriteRenderer* const renderer : gameObject->GetComponents<SpriteRenderer>())
    {
        if (mPingPong && mFrameCount == 0)
        {
            renderer->SetSheetPingPongFrame(mElapsedSeconds, mFirstFrame, mFrameRate, mLoop);
        }
        else
        {
            renderer->SetFrame(SelectAnimationFrame(
                mElapsedSeconds, mFirstFrame, mFrameCount, mFrameRate, mLoop, mPingPong));
        }
    }
}

}
