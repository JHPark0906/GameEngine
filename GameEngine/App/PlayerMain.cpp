#include "pch.h"
#include "PlayerMain.h"

#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "Application.h"
#include "GameBootstrapRegistry.h"
#include "GraphicsBackendChoice.h"
#include "IGameBootstrap.h"
#include "ProjectFile.h"
#include "../Core/TextFile.h"
#include "../Diagnostics/Debug.h"
#include "../Platform/ApplicationContent.h"
#include "../Platform/PlatformServices.h"
#include "../Serialization/ComponentSchema.h"
#include "../Serialization/RuntimeComponentFactories.h"

namespace GameEngine::App
{

namespace
{
    /// <summary>
    /// 이 실행 파일이 아는 컴포넌트 타입을 스키마 파일로 적으라는 명령줄 스위치다. 빌드가 이
    /// 실행 파일을 이 스위치로 한 번 돌려, 방금 컴파일된 코드와 같은 내용의 스키마를 얻는다.
    /// </summary>
    constexpr std::string_view EmitComponentSchemaOption = "--emit-component-schema";

    /// <summary>
    /// 명령줄이 스키마 산출을 요청했으면 파일을 쓰고 종료 코드를 답한다. 요청이 없으면 값이
    /// 없고, 플레이어는 평소대로 게임을 연다.
    ///
    /// 창도 그래픽 장치도 만들지 않는다: 이 모드는 빌드 단계에서 도는 것이라, 화면에 무엇이
    /// 뜨거나 사람의 입력을 기다리는 순간 그것은 멈춘 빌드다.
    /// </summary>
    [[nodiscard]] std::optional<int> EmitComponentSchemaIfRequested()
    {
        const std::vector<std::string> arguments =
            Platform::PlatformServices::GetCommandLineArguments();
        for (std::size_t index = 0; index < arguments.size(); ++index)
        {
            if (arguments[index] != EmitComponentSchemaOption)
            {
                continue;
            }
            if (index + 1 >= arguments.size())
            {
                Diagnostics::Debug::LogError(
                    "The component schema option needs a file path to write.");
                return EXIT_FAILURE;
            }

            const std::filesystem::path path(
                std::u8string(arguments[index + 1].begin(), arguments[index + 1].end()));
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            // 자리에 바로 쓰지 않고 임시 파일을 거쳐 이름을 바꾼다. 이 파일은 저장소 안에
            // 있고 시험 둘이 그것을 읽으므로, 제자리에 잘라 쓰면 읽는 쪽이 비었거나 잘린
            // 스키마를 온전한 것으로 알고 가져간다 — 빌드와 시험이 겹칠 때마다 이유 없이
            // 붉어지는 실패가 그것이다. 이름 바꾸기에는 파일이 비어 있는 순간이 없다.
            const Core::FileWriteResult written = Core::WriteTextFileAtomically(
                path,
                Serialization::WriteComponentSchemas(
                    Serialization::RegisteredComponentTypes()));
            if (!written)
            {
                Diagnostics::Debug::LogError(
                    "The component schema could not be written. path=", path.string(),
                    ", reason=", written.Describe());
                return EXIT_FAILURE;
            }
            Diagnostics::Debug::Log("Wrote the component schema. path=", path.string());
            return EXIT_SUCCESS;
        }
        return std::nullopt;
    }

    /// <summary>대화상자의 제목이자, 시작 실패를 알리는 상자의 제목이다.</summary>
    constexpr std::string_view GraphicsBackendDialogTitle = "Graphics backend";

    /// <summary>
    /// 이 실행에서 쓸 그래픽 백엔드를 정한다.
    ///
    /// 설정 값이 "Select"일 때만 대화상자가 뜬다. 백엔드 식별자나 "Auto"는 묻지 않고 그대로
    /// 쓰이며, 명령줄이 정했으면 설정보다 우선해 역시 묻지 않는다 — 사람이 없는 실행이 물음에
    /// 멈추지 않게 하는 길이 그것이다. 고른 값은 이번 실행에만 쓰이고 설정 파일로 돌아가지
    /// 않는다: "Select"는 매번 묻겠다는 선언이지 마지막 선택을 기억하겠다는 뜻이 아니다.
    /// </summary>
    /// <param name="settings">고른 백엔드가 요청 백엔드로 적힌다.</param>
    /// <param name="choseInDialog">사람이 대화상자에서 골랐으면 true를 받는다.</param>
    /// <returns>게임을 열지 않아야 하면 종료 코드다. 취소는 성공 종료이고, 고를 수 없으면 실패다.</returns>
    [[nodiscard]] std::optional<int> ChooseGraphicsBackend(
        ProjectSettings& settings, bool& choseInDialog)
    {
        choseInDialog = false;
        const std::vector<std::string> arguments =
            Platform::PlatformServices::GetCommandLineArguments();
        if (const std::optional<std::string> requested = FindGraphicsBackendArgument(arguments))
        {
            if (!ApplyRequestedGraphicsBackend(settings, *requested))
            {
                return EXIT_FAILURE;
            }
            return std::nullopt;
        }

        if (!RequestsGraphicsBackendDialog(settings.graphicsApi))
        {
            Diagnostics::Debug::Log(
                "Graphics backend setting. value=",
                DescribeGraphicsBackendSetting(settings.graphicsApi));
            return std::nullopt;
        }

        const std::vector<GraphicsBackendChoice> choices = ListGraphicsBackendChoices();
        if (choices.empty())
        {
            Diagnostics::Debug::LogError("No graphics backend is compiled into this build.");
            return EXIT_FAILURE;
        }

        Platform::PlatformServices::ChoiceDialogRequest request;
        request.title = GraphicsBackendDialogTitle;
        request.message = "Choose the graphics backend to start with. "
                          "Set graphicsApi to Auto to let the engine choose instead.";
        for (const GraphicsBackendChoice& choice : choices)
        {
            request.choices.push_back(DescribeGraphicsBackendChoice(choice));
        }
        request.cancelLabel = "Cancel";

        const std::optional<std::size_t> chosen =
            Platform::PlatformServices::ShowChoiceDialog(request);
        if (!ApplyGraphicsBackendChoice(settings, choices, chosen))
        {
            return chosen ? EXIT_FAILURE : EXIT_SUCCESS;
        }
        choseInDialog = true;
        return std::nullopt;
    }

    /// <summary>
    /// 사람이 고른 백엔드로 게임이 열리지 못했음을 같은 자리에 알린다. 로그만 남기면 창이 없는
    /// 실패는 아무 일도 없었던 것처럼 보인다.
    /// </summary>
    void ReportStartupFailure(const std::string_view graphicsApi)
    {
        Platform::PlatformServices::ChoiceDialogRequest request;
        request.title = GraphicsBackendDialogTitle;
        request.message = "The game could not start with " + std::string(graphicsApi) +
            ". See the log for details.";
        request.choices.push_back("OK");
        static_cast<void>(Platform::PlatformServices::ShowChoiceDialog(request));
    }
}

int RunPlayer()
{
    try
    {
        // The engine's own component types. A project's types have already registered themselves
        // from their static initializers by the time the entry point runs.
        if (!Serialization::RegisterRuntimeComponentFactories())
        {
            Diagnostics::Debug::LogError("Failed to register the engine component factories.");
            return EXIT_FAILURE;
        }

        // 이 프로세스가 아는 타입이 다 모인 첫 지점이다: 엔진의 것은 방금 등록됐고, 프로젝트의
        // 것은 정적 초기화에서 이미 등록됐다. 빌드가 요청한 스키마 산출은 여기서 답하고 끝난다.
        if (const std::optional<int> emitted = EmitComponentSchemaIfRequested())
        {
            return *emitted;
        }

        // The game is whatever this executable was built to run, found through its own content
        // rather than by looking in the directory it happens to sit in.
        const Platform::IContentSource& content = Platform::GetApplicationContent();
        const std::optional<std::filesystem::path> projectFilePath =
            ProjectFile::FindInSource(content);
        if (!projectFilePath)
        {
            return EXIT_FAILURE;
        }

        std::optional<ProjectFileData> projectFile = ProjectFile::Load(content, *projectFilePath);
        if (!projectFile)
        {
            return EXIT_FAILURE;
        }

        // 프로젝트가 bootstrap을 등록했으면 그것이 엔진 수명 주기에 끼어든다. 보통의 게임은
        // 등록하지 않고, 그러면 null이 넘어가 엔진이 장면만 돌린다.
        std::unique_ptr<IGameBootstrap> gameBootstrap = GameBootstrapRegistry::Create();

        // 백엔드 설정이 어디서 오는지는 bootstrap이 정한다: 자기 설정 파일을 가진 프로젝트는
        // 그것으로 답하고, 보통의 게임은 답하지 않아 서술자의 값이 그대로 쓰인다.
        if (gameBootstrap)
        {
            if (const std::optional<std::string> own = gameBootstrap->GetGraphicsBackendSetting())
            {
                projectFile->settings.graphicsApi = *own;
            }
        }

        bool choseInDialog = false;
        if (const std::optional<int> notStarted =
                ChooseGraphicsBackend(projectFile->settings, choseInDialog))
        {
            return *notStarted;
        }
        const std::string graphicsApi = projectFile->settings.graphicsApi;

        Application application(std::move(projectFile->settings), std::move(gameBootstrap));
        if (!application.Initialize())
        {
            Diagnostics::Debug::LogError(
                "Application initialization failed. graphicsApi=", graphicsApi);
            if (choseInDialog)
            {
                ReportStartupFailure(graphicsApi);
            }
            return EXIT_FAILURE;
        }

        return application.Run();
    }
    catch (const std::exception& exception)
    {
        Diagnostics::Debug::LogError("Unhandled exception: ", exception.what());
        return EXIT_FAILURE;
    }
}

}
