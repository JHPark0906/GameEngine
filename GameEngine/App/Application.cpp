#include "pch.h"
#include "Application.h"

#include "EngineLoop.h"
#include "IGameBootstrap.h"
#include "../Platform/ApplicationContent.h"
#include "../Platform/DirectoryContentSource.h"
// 이 파일이 합성 루트라서, 여기서 만드는 서비스의 인터페이스를 여기서 포함한다.
#include "../Platform/IAudioOutput.h"
#include "../Platform/ITextRasterizer.h"
#include "../Rendering/TextRasterizationCache.h"
#include "../Platform/PlatformServices.h"
#include "../Platform/WindowFactory.h"
#include "../Rendering/GraphicsDeviceFactory.h"
#include "../Rendering/CachedTextMeasure.h"
#include "../Rendering/IGraphicsDevice.h"
#include "../Rendering/IRenderFrontend.h"
#include "../SceneRendering/RenderFrontendFactory.h"
#include "../Diagnostics/Debug.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace GameEngine::App
{

struct Application::Implementation
{
    Implementation(
        ProjectSettings settings,
        std::string selectedBackend,
        std::unique_ptr<Rendering::IGraphicsDevice> graphicsDevice,
        std::vector<std::unique_ptr<Rendering::IRenderFrontend>> renderFrontends,
        std::unique_ptr<IGameBootstrap> gameBootstrap,
        std::unique_ptr<Platform::IAudioOutput> audioOutput,
        std::shared_ptr<Rendering::TextRasterizationCache> sceneTextCache,
        std::unique_ptr<Platform::ITextMeasure> uiTextMeasure)
        : projectSettings(std::move(settings)),
          selectedGraphicsBackend(std::move(selectedBackend)),
          window(Platform::WindowFactory::Create()),
          engineLoop(
              std::move(graphicsDevice),
              std::move(renderFrontends),
              std::move(gameBootstrap),
              std::move(audioOutput),
              std::move(sceneTextCache),
              std::move(uiTextMeasure))
    {
    }

    ProjectSettings projectSettings;
    /// <summary>실제로 장치를 만든 백엔드의 식별자다. 창 제목이 이것을 보인다.</summary>
    std::string selectedGraphicsBackend;
    std::unique_ptr<Platform::IWindow> window;
    /// <summary>프로젝트가 디렉터리일 때만 쥔다. packed 프로젝트는 애플리케이션의 것이다.</summary>
    std::unique_ptr<Platform::DirectoryContentSource> projectContent;
    EngineLoop engineLoop;
};

Application::Application(ProjectSettings projectSettings)
    : Application(std::move(projectSettings), nullptr)
{
}

Application::Application(
    ProjectSettings projectSettings,
    std::unique_ptr<IGameBootstrap> gameBootstrap)
{
    // 이 세계의 글자 배치 캐시다. 장면을 그리는 패스와 런타임의 UI 배치가 같은 것을 나눠
    // 쥔다 — 재는 캐시와 그리는 캐시가 다르면 한쪽에만 등록된 글꼴이 같은 문자열에 두 값의
    // 폭을 주고, 그 어긋남은 로그도 실패도 없이 라벨이 자기 글자보다 좁은 자리를 갖는
    // 것으로만 드러난다. 한 세계에 캐시 하나가 그래서 규칙이다.
    auto sceneTextCache = std::make_shared<Rendering::TextRasterizationCache>(
        Platform::PlatformServices::CreateTextRasterizer());
    // 프론트엔드가 이 캐시로 무엇을 그리기 전에, bootstrap에게 이 세계가 기댈 폰트를 등록할
    // 기회를 준다 — 등록이 하나도 없으면 이 세계의 텍스트는 어떤 요청이든 실패한다.
    if (gameBootstrap)
    {
        gameBootstrap->RegisterSceneFonts(*sceneTextCache);
    }
    std::string selectedGraphicsBackend;
    std::unique_ptr<Rendering::IGraphicsDevice> graphicsDevice =
        Rendering::GraphicsDeviceFactory::Create(projectSettings.graphicsApi, selectedGraphicsBackend);
    // bootstrap이 자기 프론트엔드를 제공하면 그것이 주 프레임을 그린다. 에디터의 주 프레임은
    // 장면이 아니라 UI다.
    std::vector<std::unique_ptr<Rendering::IRenderFrontend>> renderFrontends =
        gameBootstrap ? gameBootstrap->CreateRenderFrontends(sceneTextCache)
                      : std::vector<std::unique_ptr<Rendering::IRenderFrontend>>{};
    if (renderFrontends.empty())
    {
        renderFrontends = SceneRendering::RenderFrontendFactory::CreateDefault(sceneTextCache);
    }
    // 플랫폼 설비는 여기서 만들어 아래로 건넨다. 애플리케이션이 이 프로세스의 합성 루트이고,
    // 구체 구현을 고를 자격은 여기와 PlatformServices에만 있다.
    mImplementation = std::make_unique<Implementation>(
        std::move(projectSettings),
        std::move(selectedGraphicsBackend),
        std::move(graphicsDevice),
        std::move(renderFrontends),
        std::move(gameBootstrap),
        Platform::PlatformServices::CreateAudioOutput(),
        sceneTextCache,
        // 배치가 글자 크기를 물을 때 쓰는 잣대다. 위의 캐시를 그대로 나눠 쥐므로, 잰 폭은
        // 정의상 그린 폭과 같고 캐시된 배치도 한 번만 만들어진다.
        std::make_unique<Rendering::CachedTextMeasure>(
            sceneTextCache));
}

Application::~Application() = default;

bool Application::Initialize()
{
    const ProjectSettings& settings = mImplementation->projectSettings;
    Platform::WindowDescription description;
    description.title = settings.projectName;
    // 어느 백엔드로 떴는지가 제목에 보인다. 백엔드 식별자는 ASCII라 그대로 넓힌다.
    if (!mImplementation->selectedGraphicsBackend.empty())
    {
        const std::string& backend = mImplementation->selectedGraphicsBackend;
        description.title += L" [" + std::wstring(backend.begin(), backend.end()) + L"]";
    }
    description.clientWidth = settings.windowWidth;
    description.clientHeight = settings.windowHeight;
    description.chrome = settings.windowChrome;
    description.theme = settings.windowTheme;
    if (!mImplementation->window->Initialize(description))
    {
        return false;
    }

    // Where this game's files come from is decided once, here. Content packed into the executable
    // is the project, so there is no asset root under it to resolve; otherwise the asset root is
    // the directory the executable sits in, which is where staged content lands.
    const Platform::IContentSource* projectContent = nullptr;
    if (Platform::IsApplicationContentPacked())
    {
        projectContent = &Platform::GetApplicationContent();
    }
    else
    {
        mImplementation->projectContent = std::make_unique<Platform::DirectoryContentSource>(
            Platform::PlatformServices::GetExecutableDirectory());
        if (!mImplementation->projectContent->IsValid())
        {
            Diagnostics::Debug::LogError(
                "The project's asset root is missing. root=",
                mImplementation->projectContent->GetRootPath().string());
            return false;
        }
        projectContent = mImplementation->projectContent.get();
    }

    return mImplementation->engineLoop.Initialize(
        *mImplementation->window,
        *projectContent,
        settings.scenePaths,
        settings.initialSceneId,
        settings.targetFrameRate);
}

int Application::Run()
{
    while (true)
    {
        int exitCode = 0;
        switch (mImplementation->window->ProcessMessage(exitCode))
        {
        case Platform::WindowMessageResult::Quit:
            return exitCode;
        case Platform::WindowMessageResult::CloseRequested:
            // 닫아도 되는지는 무엇이 저장되지 않았는지 아는 쪽이 답한다. 프레임 밖이므로
            // 여기서 사람에게 물어도 업데이트나 렌더링이 끼어들지 않는다.
            if (mImplementation->engineLoop.ShouldClose())
            {
                mImplementation->window->RequestClose();
            }
            continue;
        case Platform::WindowMessageResult::Processed:
            continue;
        case Platform::WindowMessageResult::Idle:
            if (!mImplementation->engineLoop.RunFrame())
            {
                return -1;
            }
            break;
        }
    }
}

}
