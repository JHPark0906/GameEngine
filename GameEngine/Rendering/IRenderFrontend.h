#pragma once

namespace GameEngine::Runtime
{
class Game;
}

namespace GameEngine::Rendering
{
class RenderFrameBuilder;

/// <summary>
/// 런타임 상태에서 API 독립적 렌더 명령을 만든다.
/// 구현은 그래픽 API나 그래픽 장치에 의존해서는 안 된다.
/// </summary>
class IRenderFrontend
{
public:
    virtual ~IRenderFrontend() = default;

    IRenderFrontend(const IRenderFrontend&) = delete;
    IRenderFrontend& operator=(const IRenderFrontend&) = delete;
    IRenderFrontend(IRenderFrontend&&) = delete;
    IRenderFrontend& operator=(IRenderFrontend&&) = delete;

    /// <summary>
    /// 이 프론트엔드의 API 독립적 draw 요청을 공유 frame builder에 추가한다.
    /// 월드 오브젝트를 컬링하는 프론트엔드는 builder의 GetLockedCamera를 우선해,
    /// 그 뷰가 실제로 그릴 시점과 다른 카메라로 draw를 제거하지 않아야 한다.
    ///
    /// const가 아니다: 프론트엔드는 래스터화된 텍스트 캐시처럼 프레임을 넘나드는 상태를 쥘 수
    /// 있다. API에 의존하지 않는 준비는 여기서 한 번만 수행한다.
    /// </summary>
    virtual void Collect(const Runtime::Game& game, RenderFrameBuilder& builder) = 0;

protected:
    IRenderFrontend() = default;
};
}
