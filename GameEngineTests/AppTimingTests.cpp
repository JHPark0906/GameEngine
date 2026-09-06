#include "AppTimingTests.h"

#include "TestSupport.h"

#include <chrono>
#include <iostream>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "App/EngineLoop.h"
#include "App/IGameBootstrap.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Platform/IInput.h"
#include "Platform/ITextMeasure.h"
#include "Platform/IWindow.h"
#include "Rendering/IGraphicsDevice.h"
#include "Runtime/Game.h"
#include "App/FrameLimiter.h"
#include "App/GameTimer.h"
#include "Rendering/RenderFrame.h"

using TestSupport::Expect;

namespace
{
}

/// <summary>
/// 타이머가 흐른 시간을 델타와 누적 시간에 반영하는지 확인한다.
/// 정지 중에는 델타가 0이며, 누적 시간은 흐른 만큼 이상이다.
/// </summary>
bool RunGameTimerTests()
{
    using Clock = std::chrono::steady_clock;

    GameEngine::App::GameTimer timer;
    timer.Reset();

    const Clock::time_point start = Clock::now();
    while (Clock::now() - start < std::chrono::milliseconds(30))
    {
        // 시간이 흐르기를 기다린다. 정확한 상한은 스케줄러의 것이라 재지 않는다.
    }
    timer.Tick();
    const float delta = timer.GetDeltaTime();
    const bool measuresElapsed = delta >= 0.025f && delta < 5.0f;
    const bool accumulates = timer.GetTotalTime() >= delta * 0.9f;

    timer.Stop();
    timer.Tick();
    const bool stoppedIsZero = timer.GetDeltaTime() == 0.0f;

    timer.Start();
    timer.Tick();
    const bool resumes = timer.GetDeltaTime() >= 0.0f && timer.GetDeltaTime() < 5.0f;

    return Expect(measuresElapsed, "a tick should measure the time that passed") &&
        Expect(accumulates, "total time should cover at least the measured delta") &&
        Expect(stoppedIsZero, "a stopped timer should report zero delta") &&
        Expect(resumes, "a restarted timer should measure again");
}

/// <summary>
/// 리미터는 프레임 간격의 하한을 약속하며, 테스트가 정확히 붙들 수 있는 쪽 절반이 그것이다:
/// 초당 250으로 열 프레임이 40밀리초 안에 끝날 수는 없다. 상한은 머신의 스케줄러에 달려
/// 있으므로 일부러 단언하지 않는다.
/// </summary>
bool RunFrameLimiterTests()
{
    using Clock = std::chrono::steady_clock;

    GameEngine::App::FrameLimiter limiter(250.0f);
    const Clock::time_point start = Clock::now();
    for (int frame = 0; frame < 10; ++frame)
    {
        limiter.WaitForNextFrame();
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);

    return Expect(
        elapsed >= std::chrono::milliseconds(39),
        "ten limited frames should take at least their combined budget");
}

/// <summary>
/// 창에 그려지는 런타임이 자기 면의 크기를 언제 받는지 고정한다.
///
/// 이 검사가 겨냥하는 고장은 조용하다. 캡처 뷰가 있을 때 그 뷰가 그리는 런타임에만 크기를
/// 알리고 창의 런타임에는 알리지 않으면, 캡처 뷰를 늘 쥐고 있는 에디터는 자기 UI를 0 크기의 면
/// 위에 배치한다. 화면에는 아무것도 나타나지 않고, 원인은 UI 코드 어디에도 없다. 캡처 뷰 분기가
/// 생길 때마다 같은 방식으로 되돌아갈 수 있는 종류다.
/// </summary>
bool RunWindowSurfaceSizeTests()
{
    using GameEngine::App::ResolveWindowRuntimeSurfaceSize;
    using GameEngine::Rendering::RenderTargetSize;

    constexpr RenderTargetSize Window{ 1440, 900 };
    constexpr RenderTargetSize View{ 640, 360 };

    // 캡처 뷰가 없으면 창의 크기다.
    const RenderTargetSize withoutViews = ResolveWindowRuntimeSurfaceSize(Window, nullptr);
    const bool takesWindow = withoutViews.width == Window.width &&
        withoutViews.height == Window.height;

    // 캡처 뷰가 다른 런타임을 그려도 창에 그려지는 이 런타임은 창의 크기를 받아야 한다.
    const RenderTargetSize withOtherRuntimeView = ResolveWindowRuntimeSurfaceSize(Window, nullptr);
    const bool otherViewDoesNotBlank = withOtherRuntimeView.width == Window.width &&
        withOtherRuntimeView.height == Window.height;

    // 주 뷰가 이 런타임을 그리면 화면에 보이는 것이 그 뷰이므로 뷰의 크기가 이긴다.
    const RenderTargetSize withOwnView = ResolveWindowRuntimeSurfaceSize(Window, &View);
    const bool ownViewWins = withOwnView.width == View.width && withOwnView.height == View.height;

    return Expect(takesWindow, "a runtime drawing into the window should take the window's size") &&
        Expect(
            otherViewDoesNotBlank,
            "a capture view of another runtime should not leave the window's runtime sizeless") &&
        Expect(ownViewWins, "a capture view of this runtime should win over the window");
}

namespace
{
    /// <summary>메시지도 표면도 없는 창이다. 루프가 창에게 묻는 것은 입력뿐이다.</summary>
    class SilentWindow final : public GameEngine::Platform::IWindow
    {
    public:
        bool Initialize(const GameEngine::Platform::WindowDescription&) override { return true; }
        GameEngine::Platform::WindowMessageResult ProcessMessage(int&) const override
        {
            return GameEngine::Platform::WindowMessageResult::Idle;
        }
        [[nodiscard]] GameEngine::Platform::NativeSurface GetSurface() const override { return {}; }
        void RequestClose() override {}
        // 창이 없으니 펼칠 것도 없다. 상태만 기억해 계약을 지킨다.
        bool SetFullscreen(const bool fullscreen) override
        {
            mFullscreen = fullscreen;
            return true;
        }
        [[nodiscard]] bool IsFullscreen() const override { return mFullscreen; }
        [[nodiscard]] GameEngine::Platform::IInput& GetInput() override { return mInput; }
        bool SetTheme(
            const GameEngine::Platform::NativeSurface&,
            GameEngine::Platform::WindowTheme) const override
        {
            return true;
        }

    private:
        class NoInput final : public GameEngine::Platform::IInput
        {
        public:
            void ReadState(GameEngine::Platform::InputState& state) override { state = {}; }
        };
        NoInput mInput;
        bool mFullscreen = false;
    };

    /// <summary>
    /// 캡처 요청의 타깃 크기만 적어 두는 장치다. 뷰마다 크기를 다르게 주면 이 목록이 곧
    /// "이번 프레임에 어느 뷰가 그려졌는가"가 된다.
    /// </summary>
    class RecordingDevice final : public GameEngine::Rendering::IGraphicsDevice
    {
    public:
        bool Initialize(const GameEngine::Platform::NativeSurface&) override { return true; }
        bool BeginFrame() override { return true; }
        [[nodiscard]] GameEngine::Rendering::RenderTargetSize GetRenderTargetSize() const override
        {
            return { 800, 600 };
        }
        [[nodiscard]] GameEngine::Rendering::GraphicsDeviceCapabilities GetCapabilities()
            const override
        {
            return {};
        }
        bool Render(const GameEngine::Rendering::RenderFrame&) override { return true; }
        bool EndFrame() override { return true; }
        bool RenderToImage(
            const GameEngine::Rendering::RenderFrame& frame,
            GameEngine::Rendering::CapturedImage&,
            const CaptureRequest&) override
        {
            renderedWidths.push_back(frame.GetRenderTargetSize().width);
            return false;
        }

        std::vector<unsigned int> renderedWidths;
    };

    /// <summary>
    /// 자기 런타임을 하나 더 쥐고, 지시받은 프레임의 <c>Update</c>에서 그것을 은퇴시키는
    /// bootstrap이다. 에디터가 다른 프로젝트를 여는 순간이 이 모양이다.
    ///
    /// 은퇴한 런타임은 파괴하지 않고 옆에 옮겨 둔다. 시험이 보려는 것은 해제된 메모리를 밟는
    /// 순간이 아니라 <b>루프가 그 뷰를 그리는가</b>이고, 살려 두어야 두 순서를 모두 관찰할 수
    /// 있다.
    /// </summary>
    class RetiringBootstrap final : public GameEngine::App::IGameBootstrap
    {
    public:
        bool Initialize(GameEngine::Runtime::Game&, GameEngine::Platform::IWindow&) override
        {
            mExtraGame = std::make_unique<GameEngine::Runtime::Game>(nullptr, nullptr);
            return true;
        }
        void Update(float) override
        {
            if (retireNow)
            {
                mRetiredGame = std::move(mExtraGame);
                retireNow = false;
            }
        }
        void CollectCaptureViews(std::vector<CaptureView>& views) override
        {
            ++collectCount;
            views.push_back({ { EngineViewWidth, 32 }, std::nullopt, nullptr, true });
            if (mExtraGame)
            {
                views.push_back(
                    { { ExtraViewWidth, 32 }, std::nullopt, mExtraGame.get(), true });
            }
        }

        static constexpr unsigned int EngineViewWidth = 32;
        static constexpr unsigned int ExtraViewWidth = 64;
        bool retireNow = false;
        int collectCount = 0;

    private:
        std::unique_ptr<GameEngine::Runtime::Game> mExtraGame;
        std::unique_ptr<GameEngine::Runtime::Game> mRetiredGame;
    };
}

bool RunCaptureViewLifetimeTests()
{
    std::cout << "running capture view lifetime tests\n";

    // 이름에 프로세스 id가 들어가는 자리를 쓴다. 고정된 이름을 쓰면 같은 순간에 도는 다른
    // 시험 프로세스와 같은 디렉터리를 두고 다투게 되고, 그 다툼은 코드가 아니라 기계의 상태를
    // 재는 것이 된다.
    const TestSupport::TemporaryDirectory temporaryDirectory("capture-view-lifetime");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    std::error_code error;
    std::filesystem::create_directories(root / "Scenes", error);
    {
        std::ofstream file(root / "Scenes" / "Empty.scene", std::ios::binary);
        file << R"({"sceneName": "Empty", "gameObjects": []})";
    }
    {
        std::ofstream project(root / "CaptureViewLifetime.gameproject", std::ios::binary);
        project << R"({"projectName": "CaptureViewLifetime",)"
                   R"( "window": { "width": 800, "height": 600 }, "targetFrameRate": 60,)"
                   R"( "initialSceneId": 0, "scenes": [ { "id": 0, "path": "Scenes/Empty.scene" } ]})";
    }

    auto device = std::make_unique<RecordingDevice>();
    RecordingDevice* const deviceView = device.get();
    auto bootstrap = std::make_unique<RetiringBootstrap>();
    RetiringBootstrap* const bootstrapView = bootstrap.get();

    GameEngine::App::EngineLoop loop{
        std::move(device), {}, std::move(bootstrap), nullptr, nullptr, nullptr };

    SilentWindow window;
    const GameEngine::Platform::DirectoryContentSource content{ root };
    const std::unordered_map<unsigned int, std::filesystem::path> scenePaths{
        { 0u, "Scenes/Empty.scene" } };
    if (!Expect(loop.Initialize(window, content, scenePaths, 0u, 60.0f), "the loop should start"))
    {
        return false;
    }

    bool passed = true;

    // 평상시 프레임: 두 뷰가 모두 그려진다.
    deviceView->renderedWidths.clear();
    passed = Expect(loop.RunFrame(), "a plain frame should run") && passed;
    passed = Expect(
        deviceView->renderedWidths.size() == 2, "both views should be rendered while both live") &&
        passed;

    // 업데이트가 뷰의 런타임을 은퇴시키는 프레임이다. 그 뷰는 이번 프레임에 그려지면 안 된다 —
    // 실제 편집기에서 그 런타임은 은퇴가 아니라 파괴이고, 그리는 순간이 해제된 객체를 읽는
    // 순간이다.
    bootstrapView->retireNow = true;
    deviceView->renderedWidths.clear();
    passed = Expect(loop.RunFrame(), "the retiring frame should run") && passed;

    const auto& widths = deviceView->renderedWidths;
    const bool renderedRetired =
        std::find(widths.begin(), widths.end(), RetiringBootstrap::ExtraViewWidth) != widths.end();
    passed = Expect(
        !renderedRetired,
        "a view whose runtime the update retired should not be rendered in that frame") && passed;
    passed = Expect(
        widths.size() == 1, "the surviving view should still be rendered in that frame") && passed;

    return passed;
}

static const TestSupport::Registration gGameTimerTests{
    "AppTiming", "game timer tests should pass", RunGameTimerTests };

static const TestSupport::Registration gFrameLimiterTests{
    "AppTiming", "frame limiter tests should pass", RunFrameLimiterTests };

static const TestSupport::Registration gWindowSurfaceSizeTests{
    "AppTiming", "window surface size tests should pass", RunWindowSurfaceSizeTests };

static const TestSupport::Registration gCaptureViewLifetimeTests{
    "AppTiming", "capture view lifetime tests should pass", RunCaptureViewLifetimeTests };
