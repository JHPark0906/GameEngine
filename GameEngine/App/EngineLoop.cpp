#include "pch.h"
#include "EngineLoop.h"

#include "RenderThread.h"

#include "../Platform/IAudioOutput.h"
#include "../Platform/ITextMeasure.h"
#include "../Rendering/TextRasterizationCache.h"
#include "../Runtime/Game.h"
#include "../Runtime/Input.h"
#include "../Runtime/SceneManager.h"
#include "../Serialization/SceneSerializer.h"
#include "FrameLimiter.h"
#include "GameTimer.h"
#include "IGameBootstrap.h"
#include "../Rendering/IGraphicsDevice.h"
#include "../Rendering/IRenderFrontend.h"
#include "../Rendering/RenderFrame.h"
#include "../Rendering/RenderFrameBuilder.h"
#include "../SceneRendering/RenderFrontendFactory.h"
#include "../Diagnostics/Debug.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace GameEngine::App
{

Rendering::RenderTargetSize ResolveWindowRuntimeSurfaceSize(
    const Rendering::RenderTargetSize windowSize,
    const Rendering::RenderTargetSize* const primaryViewSize)
{
    return primaryViewSize ? *primaryViewSize : windowSize;
}

EngineLoop::EngineLoop(
    std::unique_ptr<Rendering::IGraphicsDevice> graphicsDevice,
    std::vector<std::unique_ptr<Rendering::IRenderFrontend>> renderFrontends,
    std::unique_ptr<IGameBootstrap> gameBootstrap,
    std::unique_ptr<Platform::IAudioOutput> audioOutput,
    std::shared_ptr<Rendering::TextRasterizationCache> sceneTextCache,
    std::unique_ptr<Platform::ITextMeasure> uiTextMeasure)
    : mGameBootstrap(std::move(gameBootstrap)),
      mGraphicsDevice(std::move(graphicsDevice)),
      mRenderFrontends(std::move(renderFrontends)),
      mSceneFrontends(
          SceneRendering::RenderFrontendFactory::CreateDefault(std::move(sceneTextCache))),
      mGame(std::make_unique<Runtime::Game>(
          std::move(audioOutput), std::move(uiTextMeasure))),
      mGameTimer(std::make_unique<GameTimer>())
{
    // 장면 파일 형식은 런타임 위의 것이라 여기서 끼운다.
    mGame->SetSceneLoader(Serialization::SceneSerializer::MakeSceneLoader());
}

EngineLoop::~EngineLoop()
{
    // 스레드가 장치보다 먼저 멈춰야 한다. 멤버 선언 순서에 기대지 않고 여기서 순서를 적는 이유는,
    // 뒤집혔을 때의 실패가 이미 사라진 장치를 부르는 종료 크래시이고 재현되지 않기 때문이다.
    if (mRenderThread)
    {
        mRenderThread->Stop();
    }
}

bool EngineLoop::Initialize(
    Platform::IWindow& window,
    const Platform::IContentSource& projectContent,
    const std::unordered_map<unsigned int, std::filesystem::path>& scenePaths,
    const unsigned int initialSceneId,
    const float targetFrameRate)
{
    if (!mGame->Initialize(projectContent, scenePaths) ||
        mGame->LoadScene(initialSceneId) != Runtime::SceneLoadResult::Loaded)
    {
        Diagnostics::Debug::LogError("Failed to initialize the game runtime.");
        return false;
    }

    if (mGameBootstrap && !mGameBootstrap->Initialize(*mGame, window))
    {
        Diagnostics::Debug::LogError("Failed to initialize the game project bootstrap.");
        return false;
    }

    const Platform::NativeSurface renderTargetSurface = window.GetSurface();
    if (!mGraphicsDevice || !mGraphicsDevice->Initialize(renderTargetSurface))
    {
        Diagnostics::Debug::LogError("Failed to initialize the graphics device.");
        return false;
    }

    mWindow = &window;
    mFrameLimiter = std::make_unique<FrameLimiter>(targetFrameRate);
    // 창에 present하는 실행에서만 렌더 스레드가 선다. 그것이 오늘의 모든 게임 프로젝트이며
    // 에디터도 포함된다. 창이 면을 주지 않는 실행 — 시험이 세우는 조용한 창 — 에서는 스레드가
    // 서지 않고, 이 스레드가 장치의 유일한 사용자가 된다.
    if (renderTargetSurface.kind != Platform::NativeSurfaceKind::None)
    {
        mRenderThread = std::make_unique<RenderThread>(*mGraphicsDevice);
        mRenderThread->Start();
    }

    mGameTimer->Reset();
    mGameTimer->Start();
    return true;
}

bool EngineLoop::FailFrame(const char* const reason)
{
    // 프레임 실패는 곧 종료다: Application::Run이 이 false를 받고 빠져나간다.
    // 종료 전에 bootstrap에 알려 저장되지 않은 작업물을 지킬 기회를 준다.
    Diagnostics::Debug::LogError(reason, "; the run is ending.");
    if (mGameBootstrap)
    {
        mGameBootstrap->OnUnrecoverableFailure(reason);
    }
    return false;
}

bool EngineLoop::ShouldClose()
{
    return !mGameBootstrap || mGameBootstrap->ShouldClose();
}

bool EngineLoop::RunFrame()
{
    mGameTimer->Tick();

    // Input is taken once, before anything updates, so every object that asks during this frame is
    // answered from the same instant. Reading it takes the typed text and wheel motion accumulated
    // since the previous frame, which is why exactly one place may read it.
    if (mWindow)
    {
        mGame->GetInput().BeginFrame(mWindow->GetInput());
    }


    // 지난 프레임에 렌더 스레드가 그려 둔 뷰 픽셀을 받는다. 셸의 상태는 이 스레드에서
    // 갱신하므로 렌더 스레드는 픽셀을 큐에 놓기만 한다. 뷰에는 한 프레임의 지연이 있다.
    if (mRenderThread && mGameBootstrap)
    {
        mRenderThread->TakeCapturedViews(mCapturedViews);
        for (const CapturedView& captured : mCapturedViews)
        {
            mGameBootstrap->OnViewCaptured(captured.channel, captured.image);
        }
    }

    // 캡처 뷰는 어느 모드에서든 먼저 돈다: RenderToImage는 BeginFrame과 EndFrame 사이에 설 수
    // 없으므로, 창에 그리는 에디터가 뷰 이미지를 원하면 그 캡처는 주 프레임 앞이어야 한다.
    std::vector<IGameBootstrap::CaptureView> captureViews;
    if (mGameBootstrap)
    {
        mGameBootstrap->CollectCaptureViews(captureViews);
    }
    // present할 면이 없으면 렌더 스레드도 없다. 그 실행에서는 창에 알릴 크기도 없다.
    // 창에 그려지는 런타임은 자기 면의 크기를 안다. 캡처 뷰가 있다고 해서 달라지지 않는다 —
    // 에디터처럼 뷰를 여럿 쥔 애플리케이션에서도 자기 화면 공간 UI는 창 픽셀 위에 놓인다.
    if (mRenderThread)
    {
        // 주 뷰가 바로 이 런타임을 그리면 화면에 보이는 것은 그 뷰이므로, 그 크기가 이긴다.
        const bool primaryDrawsThisRuntime = !captureViews.empty() &&
            (captureViews.front().game == nullptr || captureViews.front().game == mGame.get());
        const Rendering::RenderTargetSize surfaceSize = ResolveWindowRuntimeSurfaceSize(
            GetRenderTargetSize(),
            primaryDrawsThisRuntime ? &captureViews.front().size : nullptr);
        mGame->SetRenderAspectRatio(surfaceSize.GetAspectRatio());
        mGame->SetRenderSurfaceSize(
            static_cast<float>(surfaceSize.width), static_cast<float>(surfaceSize.height));
    }

    // 뷰가 그리는 런타임도 자기가 보일 크기를 알아야 한다. 그 런타임이 위의 게임이면 방금 같은
    // 값을 받았고, 아니면 — 에디터가 편집 대상을 그리는 경우다 — 여기서 받는다.
    if (!captureViews.empty())
    {
        const IGameBootstrap::CaptureView& primary = captureViews.front();
        Runtime::Game& primaryGame = primary.game ? *primary.game : *mGame;
        primaryGame.SetRenderAspectRatio(primary.size.GetAspectRatio());
        // 화면 공간 UI는 비율이 아니라 픽셀 위에 놓이므로, 같은 자리에서 크기 자체도 알린다.
        primaryGame.SetRenderSurfaceSize(
            static_cast<float>(primary.size.width), static_cast<float>(primary.size.height));
    }

    mGame->Update(mGameTimer->GetDeltaTime());
    if (mGameBootstrap)
    {
        mGameBootstrap->Update(mGameTimer->GetDeltaTime());

        // 뷰 목록을 업데이트 뒤에 다시 받는다. 위의 목록은 업데이트 전에 면 크기를 알리기
        // 위한 것이고, 그 사이 업데이트가 뷰가 가리키던 런타임을 없앨 수 있다 — 에디터가
        // 다른 프로젝트를 여는 것이 그런 동작이다. 아래 루프는 포인터를 역참조하므로
        // 업데이트 뒤의 목록이어야 한다.
        captureViews.clear();
        mGameBootstrap->CollectCaptureViews(captureViews);
    }

    // 뷰의 프레임은 이 스레드에서 만든다. Collect가 라이브 런타임을 읽으므로 그 일은 넘길 수
    // 없고, 넘어가는 것은 다 만들어진 값뿐이다 — 프레임 자신이 그렇듯이.
    std::vector<CaptureJob> captureJobs;
    captureJobs.reserve(captureViews.size());
    for (std::size_t viewIndex = 0; viewIndex < captureViews.size(); ++viewIndex)
    {
        const IGameBootstrap::CaptureView& view = captureViews[viewIndex];
        if (!view.size.IsValid())
        {
            continue;
        }

        Rendering::RenderFrameBuilder frameBuilder;
        frameBuilder.SetRenderTargetSize(view.size);
        frameBuilder.SetOverlayEnabled(view.includeOverlay);
        if (view.camera)
        {
            // 뷰의 카메라가 장면의 카메라 선택을 이긴다. 같은 장면을 다른 시점에서 보는 것이
            // 이 뷰의 존재 이유다.
            frameBuilder.LockCamera(*view.camera);
        }
        // 뷰가 가리키는 런타임을 그린다. 지정이 없으면 엔진이 부팅한 게임이다.
        const Runtime::Game& viewGame = view.game ? *view.game : *mGame;
        for (const std::unique_ptr<Rendering::IRenderFrontend>& sceneFrontend : mSceneFrontends)
        {
            sceneFrontend->Collect(viewGame, frameBuilder);
        }
        // 채널은 뷰의 자리 번호다. 숨겨진 뷰는 크기가 무효라 여기서 빠지지만 번호는 그대로여서,
        // 돌아온 픽셀이 어느 뷰의 것인지 말할 수 있다.
        captureJobs.push_back(
            { std::move(frameBuilder).Build(), static_cast<unsigned int>(viewIndex) });
    }

    // 렌더 스레드가 없는 실행은 이 스레드가 장치의 유일한 사용자라
    // 여기서 직접 그린다.
    if (!mRenderThread)
    {
        for (const CaptureJob& job : captureJobs)
        {
            const Rendering::RenderFrame& frame = job.frame;
            const std::size_t viewIndex = job.channel;

            Rendering::CapturedImage image;
            const Rendering::IGraphicsDevice::CaptureRequest request{ job.channel, false };
            if (mGraphicsDevice->RenderToImage(frame, image, request) && image.IsValid())
            {
                mGameBootstrap->OnViewCaptured(viewIndex, image);
            }
        }
    }
    if (!mRenderThread)
    {
        // present할 창이 없다. 캡처가 이 프레임의 출력 전부였고, vsync가 없으므로 리미터가
        // 유일한 pacing이다.
        mFrameLimiter->WaitForNextFrame();
        return true;
    }

    // 게임 업데이트는 위에서 이미 돌았다. 여기서부터는 주 프레임의 수집과 제출뿐이다.
    Rendering::RenderFrameBuilder frameBuilder;
    frameBuilder.SetRenderTargetSize(GetRenderTargetSize());
    for (std::unique_ptr<Rendering::IRenderFrontend>& renderFrontend : mRenderFrontends)
    {
        renderFrontend->Collect(*mGame, frameBuilder);
    }
    Rendering::RenderFrame frame = std::move(frameBuilder).Build();

    // 앞선 프레임이 실패했으면 여기서 끝난다. 실패를 알리고 프로세스를 끝내는 일은 게임
    // 스레드의 것이라, 렌더 스레드는 사실만 남기고 물러난다.
    if (mRenderThread->HasFailed())
    {
        return FailFrame(mRenderThread->GetFailureReason());
    }
    // 자리가 비어 있으면 곧바로 돌아온다. 차 있으면 렌더 쪽이 앞 프레임을 끝낼 때까지 기다리는데,
    // 그 기다림이 곧 이 루프의 pacing이다 — vsync가 present 안에서 이미 하고 있던 그 일이다.
    if (!mRenderThread->Submit({ std::move(captureJobs), std::move(frame) }))
    {
        return FailFrame(
            mRenderThread->GetFailureReason() ? mRenderThread->GetFailureReason()
                                              : "The render thread stopped accepting frames");
    }

    // 리미터는 목표보다 빠른 화면을 목표로 늦춘다. 느린 화면에서는 렌더 스레드가 present 안에서
    // 이미 기다리고 있으므로, 그 대기는 위의 Submit이 대신 문다.
    mFrameLimiter->WaitForNextFrame();
    return true;
}

Rendering::RenderTargetSize EngineLoop::GetRenderTargetSize() const
{
    // 장치는 렌더 스레드의 것이므로 그 스레드가 발행한 값을 읽는다. 스레드가 없는 실행에서는
    // 이 스레드가 장치의 유일한 사용자라 직접 묻는다.
    return mRenderThread ? mRenderThread->GetRenderTargetSize()
                         : mGraphicsDevice->GetRenderTargetSize();
}

}
