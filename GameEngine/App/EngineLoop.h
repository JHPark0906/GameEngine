#pragma once

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

#include "../Platform/IContentSource.h"
#include "../Rendering/RenderFrame.h"
#include "../Platform/IWindow.h"
#include "RenderSubmission.h"

namespace GameEngine::Platform
{
class IAudioOutput;
class ITextMeasure;
class ITextRasterizer;
}

namespace GameEngine::Runtime
{
class Game;
}

namespace GameEngine::Rendering
{
class IGraphicsDevice;
class IRenderFrontend;
class TextRasterizationCache;
}

namespace GameEngine::App
{

class FrameLimiter;
class GameTimer;
class IGameBootstrap;
class RenderThread;

/// <summary>
/// 게임 런타임 업데이트와 렌더 패스 실행 순서를 조정하는 프레임 루프이다.
/// </summary>

/// <summary>
/// 창에 그려지는 런타임이 이번 프레임에 알아야 할 면 크기다.
///
/// 그 런타임은 캡처 뷰가 있든 없든 자기 창의 픽셀 위에 그린다 — 화면 공간 UI는 비율이 아니라
/// 픽셀에 놓이므로, 크기를 모르면 0 크기의 면 위에서 배치되어 아무 데도 보이지 않는다. 예외는
/// 주 캡처 뷰가 바로 그 런타임을 그리는 경우뿐이다: 그때 화면에 보이는 것은 그 뷰이므로 뷰의
/// 크기가 창의 크기를 이긴다.
/// </summary>
/// <param name="windowSize">창의 렌더 타깃 크기다.</param>
/// <param name="primaryViewSize">
/// 주 캡처 뷰가 이 런타임을 그릴 때 그 뷰의 크기이고, 캡처 뷰가 없거나 다른 런타임을 그리면
/// null이다.
/// </param>
[[nodiscard]] Rendering::RenderTargetSize ResolveWindowRuntimeSurfaceSize(
    Rendering::RenderTargetSize windowSize, const Rendering::RenderTargetSize* primaryViewSize);

class EngineLoop final
{
public:
    /// <summary>루프가 소유할 그래픽 장치, 렌더 패스, 오디오 출력을 인수한다.</summary>
    /// <param name="graphicsDevice">프레임을 출력할 그래픽 장치이다.</param>
    /// <param name="renderFrontends">주 프레임을 그리는 프론트엔드들이다.</param>
    /// <param name="gameBootstrap">프로젝트가 끼우는 시동 훅이다. null일 수 있다.</param>
    /// <param name="audioOutput">
    /// 런타임이 소리를 낼 출력이다. 장치와 프론트엔드와 같은 자리에서 주입된다: 무엇을 쓸지
    /// 고르는 일은 애플리케이션의 것이고, 그래야 런타임이 플랫폼 구현을 이름 부르지 않는다.
    /// </param>
    /// <param name="sceneTextCache">
    /// 캡처 뷰를 그리는 장면 프론트엔드의 글자 배치 캐시다. 런타임의 UI 배치가 같은 것을
    /// 나눠 쥐어야 재는 폭과 그리는 폭이 갈라지지 않으므로, 만드는 일은 애플리케이션의 것이다.
    /// </param>
    EngineLoop(
        std::unique_ptr<Rendering::IGraphicsDevice> graphicsDevice,
        std::vector<std::unique_ptr<Rendering::IRenderFrontend>> renderFrontends,
        std::unique_ptr<IGameBootstrap> gameBootstrap,
        std::unique_ptr<Platform::IAudioOutput> audioOutput,
        std::shared_ptr<Rendering::TextRasterizationCache> sceneTextCache,
        std::unique_ptr<Platform::ITextMeasure> uiTextMeasure);
    ~EngineLoop();

    EngineLoop(const EngineLoop&) = delete;
    EngineLoop& operator=(const EngineLoop&) = delete;
    EngineLoop(EngineLoop&&) = delete;
    EngineLoop& operator=(EngineLoop&&) = delete;

    /// <summary>그래픽 장치, 렌더 패스 및 게임 런타임을 초기화한다.</summary>
    /// <param name="window">그래픽 장치가 출력할 기본 창이다.</param>
    /// <param name="projectContent">이 게임의 파일들. 소유하지 않는다.</param>
    /// <param name="scenePaths">장면 ID와 장면 파일 경로의 대응표이다.</param>
    /// <param name="initialSceneId">초기화 직후 불러올 장면 ID이다.</param>
    /// <param name="targetFrameRate">프로젝트가 목표로 하는 초당 프레임 수이다.</param>
    /// <returns>모든 구성 요소가 초기화되고 초기 장면이 로드되었으면 true이다.</returns>
    [[nodiscard]] bool Initialize(
        Platform::IWindow& window,
        const Platform::IContentSource& projectContent,
        const std::unordered_map<unsigned int, std::filesystem::path>& scenePaths,
        unsigned int initialSceneId,
        float targetFrameRate);
    /// <summary>시간을 갱신하고 게임 업데이트와 렌더링을 한 프레임 실행한다.</summary>
    /// <returns>프레임 출력에 성공했으면 true이다.</returns>
    [[nodiscard]] bool RunFrame();

    /// <summary>
    /// 사람이 창을 닫으려 할 때, 닫아도 되는지 프로젝트에게 묻는다. 끼운 것이 없으면 true다.
    /// 프레임 밖에서 불린다 — 묻는 동안 업데이트나 렌더링이 도중에 끼어들지 않는다.
    /// </summary>
    /// <returns>닫아도 되면 true다.</returns>
    [[nodiscard]] bool ShouldClose();

private:
    /// <summary>
    /// 프레임을 실패로 끝낸다. 실패를 로그로 남기고, bootstrap에 곧 끝난다는 것을 알린 뒤
    /// false를 돌려준다 — 그 false가 곧 프로세스의 종료이므로, 알림은 여기가 마지막 자리다.
    /// </summary>
    /// <param name="reason">무엇이 실패했는지다.</param>
    /// <returns>언제나 false이다.</returns>
    [[nodiscard]] bool FailFrame(const char* reason);

    /// <summary>
    /// 이번 프레임을 만들 때 쓸 렌더 타깃 크기다. 렌더 스레드가 서 있으면 그 스레드가 발행한
    /// 값이고, 아니면 장치에게 직접 묻는다.
    /// </summary>
    [[nodiscard]] Rendering::RenderTargetSize GetRenderTargetSize() const;

    std::unique_ptr<IGameBootstrap> mGameBootstrap;
    std::unique_ptr<Rendering::IGraphicsDevice> mGraphicsDevice;
    std::vector<std::unique_ptr<Rendering::IRenderFrontend>> mRenderFrontends;

    /// <summary>
    /// 캡처 뷰를 그리는 장면 프론트엔드들이다. 주 프레임의 프론트엔드와 분리된 이유는
    /// 에디터에 있다: 에디터의 주 프레임은 UI만 그리지만, 뷰 안에는 장면이 보여야 한다.
    /// </summary>
    std::vector<std::unique_ptr<Rendering::IRenderFrontend>> mSceneFrontends;
    std::unique_ptr<Runtime::Game> mGame;
    std::unique_ptr<GameTimer> mGameTimer;
    std::unique_ptr<FrameLimiter> mFrameLimiter;

    /// <summary>
    /// 주 프레임과 캡처를 제출하는 렌더 스레드다. 창에 present하는 실행에서만 사용한다.
    /// 게임 스레드는 캡처 프레임의 수집을 맡고, 렌더 스레드는 완성된 프레임을 장치에 제출한다.
    /// 소멸자는 장치를 파괴하기 전에 이 스레드를 멈춰 해제된 장치에 접근하지 않게 한다.
    /// </summary>
    std::unique_ptr<RenderThread> mRenderThread;

    /// <summary>
    /// 렌더 스레드에게서 받아 온 뷰 픽셀을 담는 자리다. 프레임마다 비우고 다시 채우므로,
    /// 매 프레임 벡터를 새로 만들지 않는다.
    /// </summary>
    std::vector<CapturedView> mCapturedViews;

    /// <summary>입력이 도착하는 곳이다. 소유하지 않는다: 애플리케이션이 루프보다 오래 산다.</summary>
    Platform::IWindow* mWindow = nullptr;
};

}
