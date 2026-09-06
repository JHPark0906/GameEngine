#include "Rules/EditorProjectBuild.h"

#include "Build/CMakeLocation.h"
#include "Diagnostics/Debug.h"
#include "Platform/PlatformServices.h"

// 빌드 트리의 자리는 빌드할 때만 알 수 있다. 정의가 없는 채로 컴파일되는 경우가 하나 있는데 —
// 이 파일을 가져다 쓰는 시험 실행 파일이다 — 그때는 빈 값이 맞다: 시험은 트리를 모르는 에디터가
// 어떻게 답하는지도 함께 본다.
#ifndef GAMEEDITOR_SOURCE_ROOT
#define GAMEEDITOR_SOURCE_ROOT ""
#endif
#ifndef GAMEEDITOR_BINARY_ROOT
#define GAMEEDITOR_BINARY_ROOT ""
#endif
#ifndef GAMEEDITOR_BUILD_CONFIGURATION
#define GAMEEDITOR_BUILD_CONFIGURATION ""
#endif

namespace GameEditor
{

namespace
{
    /// <summary>
    /// 경로를 UTF-8 바이트로 바꾼다. <c>string()</c>이 아닌 이유는 그것이 이 기계의 ANSI
    /// 코드페이지로 바꾸기 때문이다 — 한글이 든 경로가 그 길로 가면 프로세스 계층이 UTF-8로
    /// 읽어 깨진 인자가 된다.
    /// </summary>
    [[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string value = path.u8string();
        return std::string(value.begin(), value.end());
    }

    /// <summary>
    /// 실패했을 때 되짚어 줄 줄 수다. 컴파일 오류 하나는 여러 줄로 나오고, 실패를 낸 줄은
    /// 대개 끝에 있다. 전부 쥐고 있으면 성공하는 빌드에서 아무도 읽지 않을 수만 줄을 들고
    /// 있게 된다.
    /// </summary>
    constexpr std::size_t FailureOutputLineCount = 40;
}

EditorBuildTree EditorBuildTree::OfThisEditor()
{
    EditorBuildTree tree;
    tree.sourceRoot = std::filesystem::path(std::u8string_view(
        reinterpret_cast<const char8_t*>(GAMEEDITOR_SOURCE_ROOT)));
    tree.buildDirectory = std::filesystem::path(std::u8string_view(
        reinterpret_cast<const char8_t*>(GAMEEDITOR_BINARY_ROOT)));
    tree.configuration = GAMEEDITOR_BUILD_CONFIGURATION;
    return tree;
}

bool EditorBuildTree::IsKnown() const
{
    return !sourceRoot.empty() && !buildDirectory.empty() && !configuration.empty();
}

GameEngine::Platform::ProcessRequest MakeConfigureRequest(
    const std::filesystem::path& cmakePath, const EditorBuildTree& tree,
    const std::filesystem::path& projectRoot)
{
    GameEngine::Platform::ProcessRequest request;
    request.executable = cmakePath;
    request.arguments = {
        "-S", PathToUtf8(tree.sourceRoot),
        "-B", PathToUtf8(tree.buildDirectory),
        "-DGAMEEDITOR_PROJECT_DIRECTORY=" + PathToUtf8(projectRoot)
    };
    request.workingDirectory = tree.sourceRoot;
    return request;
}

GameEngine::Platform::ProcessRequest MakeBuildRequest(
    const std::filesystem::path& cmakePath, const EditorBuildTree& tree,
    const std::string_view targetName)
{
    GameEngine::Platform::ProcessRequest request;
    request.executable = cmakePath;
    request.arguments = {
        "--build", PathToUtf8(tree.buildDirectory),
        "--config", tree.configuration,
        "--target", std::string(targetName)
    };
    request.workingDirectory = tree.sourceRoot;
    return request;
}

ProjectBuild::ProjectBuild() = default;

ProjectBuild::~ProjectBuild() = default;

bool ProjectBuild::Start(
    const EditorBuildTree& tree, const std::filesystem::path& projectRoot,
    const std::string_view targetName)
{
    if (mStep != ProjectBuildStep::Idle)
    {
        GameEngine::Diagnostics::Debug::LogError("A build is already running.");
        return false;
    }
    if (!tree.IsKnown())
    {
        GameEngine::Diagnostics::Debug::LogError(
            "This editor does not know the build tree it came from, so it cannot build. Build it "
            "from a CMake tree to use this button.");
        return false;
    }
    if (targetName.empty())
    {
        GameEngine::Diagnostics::Debug::LogError("The project has no name to build.");
        return false;
    }

    // 이 트리를 세운 cmake에게 묻는다. 캐시가 그것을 적어 두므로, PATH에 다른 판이 먼저 있어도
    // 트리를 읽을 수 있는 쪽이 뽑힌다.
    const std::optional<std::filesystem::path> cmakePath =
        GameEngine::Build::ResolveCMakePath({}, tree.buildDirectory);
    if (!cmakePath)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "No cmake was found to build with. buildDirectory=", tree.buildDirectory.string());
        return false;
    }

    mProcess = GameEngine::Platform::PlatformServices::CreateProcessRunner();
    if (!mProcess)
    {
        GameEngine::Diagnostics::Debug::LogError("This platform cannot run a build.");
        return false;
    }
    mTree = tree;
    mCMakePath = *cmakePath;
    mProjectRoot = projectRoot;
    mTargetName = targetName;
    mRecentOutput.clear();

    if (!mProcess->Start(MakeConfigureRequest(mCMakePath, mTree, mProjectRoot)))
    {
        mProcess.reset();
        return false;
    }
    mStep = ProjectBuildStep::Configuring;
    GameEngine::Diagnostics::Debug::Log(
        "Configuring the build tree for ", mTargetName, ". cmake=", mCMakePath.string());
    return true;
}

bool ProjectBuild::StartCompileStep()
{
    if (!mProcess->Start(MakeBuildRequest(mCMakePath, mTree, mTargetName)))
    {
        return false;
    }
    mStep = ProjectBuildStep::Compiling;
    GameEngine::Diagnostics::Debug::Log("Building ", mTargetName, ".");
    return true;
}

void ProjectBuild::Poll()
{
    if (mStep == ProjectBuildStep::Idle || !mProcess)
    {
        return;
    }

    mProcess->Poll([this](const std::string_view line)
    {
        // 도구가 낸 줄을 그대로 넘긴다. 여기서 고르거나 다듬으면 사람이 콘솔에서 보는 것과
        // 터미널에서 보는 것이 달라진다.
        GameEngine::Diagnostics::Debug::Log(line);
        // 실행 중 메시지 설정이 꺼져 있을 때만 사본을 둔다.
        // 켜져 있으면 콘솔이 이미 같은 줄을 보관한다.
        if (!GameEngine::Diagnostics::Debug::AreMessagesEnabled())
        {
            mRecentOutput.emplace_back(line);
            if (mRecentOutput.size() > FailureOutputLineCount)
            {
                mRecentOutput.pop_front();
            }
        }
    });

    const std::optional<int> exitCode = mProcess->TakeExitCode();
    if (!exitCode)
    {
        return;
    }

    if (*exitCode != 0)
    {
        ReportFailureOutput();
        GameEngine::Diagnostics::Debug::LogError(
            mStep == ProjectBuildStep::Configuring ? "Configure failed. exitCode="
                                                   : "Build failed. exitCode=",
            *exitCode);
        mStep = ProjectBuildStep::Idle;
        mProcess.reset();
        return;
    }

    mRecentOutput.clear();
    if (mStep == ProjectBuildStep::Configuring)
    {
        if (!StartCompileStep())
        {
            mStep = ProjectBuildStep::Idle;
            mProcess.reset();
        }
        return;
    }

    GameEngine::Diagnostics::Debug::Log("Build succeeded. target=", mTargetName);
    // 컴포넌트는 에디터 실행 파일 안에 링크되어 있다. 그래서 이 빌드가 성공해도 지금 돌고 있는
    // 에디터의 Add Component 목록은 그대로다 — 이 버튼이 답하는 것은 "컴파일이 되는가"이지
    // "지금 쓸 수 있는가"가 아니다.
    GameEngine::Diagnostics::Debug::LogWarning(
        "Close and rebuild the editor to use the new components in Play mode.");
    mStep = ProjectBuildStep::Idle;
    mProcess.reset();
}

void ProjectBuild::ReportFailureOutput()
{
    for (const std::string& line : mRecentOutput)
    {
        GameEngine::Diagnostics::Debug::LogError(line);
    }
    mRecentOutput.clear();
}

void ProjectBuild::Cancel()
{
    if (!mProcess)
    {
        return;
    }
    mProcess->RequestCancel();
    GameEngine::Diagnostics::Debug::LogWarning("The build was cancelled.");
    mStep = ProjectBuildStep::Idle;
    mRecentOutput.clear();
    mProcess.reset();
}

bool ProjectBuild::IsRunning() const
{
    return mStep != ProjectBuildStep::Idle;
}

ProjectBuildStep ProjectBuild::GetStep() const
{
    return mStep;
}

}
