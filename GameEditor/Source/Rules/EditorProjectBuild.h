#pragma once

// editor-layer: 0 (Rules)

#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "Platform/IProcessRunner.h"

namespace GameEditor
{

/// <summary>빌드가 어느 단계에 있는지다.</summary>
enum class ProjectBuildStep
{
    /// <summary>돌고 있지 않다.</summary>
    Idle,
    /// <summary>빌드 트리를 다시 만드는 중이다.</summary>
    Configuring,
    /// <summary>프로젝트의 타깃을 컴파일하는 중이다.</summary>
    Compiling
};

/// <summary>
/// 이 에디터가 나온 빌드 트리다.
///
/// 세 값 모두 빌드할 때 정해져 실행 파일에 박힌다. 실행 파일 경로에서 거꾸로 짚어 볼 수도
/// 있지만 그것은 추측이다 — 빌드 디렉터리의 자리는 생성기와 프리셋이 정하고, 구성 이름은
/// 실행 파일 어디에도 적혀 있지 않다. 아는 쪽이 말해 주는 편이 맞다.
/// </summary>
struct EditorBuildTree
{
    /// <summary>최상위 CMakeLists.txt가 있는 디렉터리다.</summary>
    std::filesystem::path sourceRoot;
    /// <summary>CMakeCache.txt가 있는 디렉터리다. 이 에디터가 여기서 나왔다.</summary>
    std::filesystem::path buildDirectory;
    /// <summary>Debug나 Release다. 여러 구성을 한 트리에 두는 생성기에서 필요하다.</summary>
    std::string configuration;

    /// <summary>이 실행 파일을 만든 트리다.</summary>
    [[nodiscard]] static EditorBuildTree OfThisEditor();

    /// <summary>
    /// 빌드를 걸 수 있을 만큼 아는지다. 빌드 트리 없이 배포된 에디터는 이것이 거짓이고,
    /// 그때는 빌드 버튼이 이유를 말하고 아무것도 하지 않는다.
    /// </summary>
    [[nodiscard]] bool IsKnown() const;
};

/// <summary>
/// 빌드 트리를 이 프로젝트로 맞추는 명령이다.
///
/// 프리셋이 아니라 트리를 직접 가리킨다: 생성기도 도구 모음도 이미 캐시에 있고, 프리셋
/// 이름을 다시 고르는 것은 에디터가 어느 프리셋에서 나왔는지를 한 번 더 추측하는 일이다.
///
/// 이 단계가 필요한 이유는 열려 있는 프로젝트가 이 트리가 알고 있는 프로젝트와 다를 수 있고,
/// 방금 첫 컴포넌트를 얻은 프로젝트에는 빌드할 타깃이 아직 없기 때문이다.
/// </summary>
[[nodiscard]] GameEngine::Platform::ProcessRequest MakeConfigureRequest(
    const std::filesystem::path& cmakePath, const EditorBuildTree& tree,
    const std::filesystem::path& projectRoot);

/// <summary>프로젝트의 타깃 하나를 컴파일하는 명령이다.</summary>
[[nodiscard]] GameEngine::Platform::ProcessRequest MakeBuildRequest(
    const std::filesystem::path& cmakePath, const EditorBuildTree& tree,
    std::string_view targetName);

/// <summary>
/// 열린 프로젝트를 빌드하는 일 하나다.
///
/// 두 단계를 이어 돌리고, 나오는 줄을 로그로 넘기고, 실패하면 거기서 멈춘다. 프레임을 막지
/// 않는 것은 <see cref="GameEngine::Platform::IProcessRunner"/>가 맡고, 이 타입은 그 위에서
/// 「configure가 성공했으면 컴파일한다」는 순서만 쥔다.
/// </summary>
class ProjectBuild final
{
public:
    ProjectBuild();
    ~ProjectBuild();

    ProjectBuild(const ProjectBuild&) = delete;
    ProjectBuild& operator=(const ProjectBuild&) = delete;
    ProjectBuild(ProjectBuild&&) = delete;
    ProjectBuild& operator=(ProjectBuild&&) = delete;

    /// <summary>
    /// 빌드를 시작한다. 이미 돌고 있거나, 트리를 모르거나, cmake를 찾지 못하면 이유를 로그에
    /// 남기고 거짓이다.
    /// </summary>
    /// <param name="tree">이 에디터가 나온 빌드 트리다.</param>
    /// <param name="projectRoot">.gameproject가 있는 디렉터리다.</param>
    /// <param name="targetName">컴파일할 CMake 타깃이다. 프로젝트 이름과 같다.</param>
    [[nodiscard]] bool Start(
        const EditorBuildTree& tree, const std::filesystem::path& projectRoot,
        std::string_view targetName);

    /// <summary>
    /// 프레임마다 부른다. 모인 줄을 로그로 넘기고, 한 단계가 끝났으면 다음 단계로 가거나
    /// 결과를 말한다.
    /// </summary>
    void Poll();

    /// <summary>지금 그만둔다. cmake가 낳은 도구들까지 함께 끝난다.</summary>
    void Cancel();

    [[nodiscard]] bool IsRunning() const;
    [[nodiscard]] ProjectBuildStep GetStep() const;

private:
    /// <summary>컴파일 단계를 시작한다. configure가 성공한 뒤에만 불린다.</summary>
    [[nodiscard]] bool StartCompileStep();

    /// <summary>
    /// 실패한 단계의 마지막 줄들을 오류로 다시 남긴다. 일반 로그가 컴파일되어 들어가지 않는
    /// 빌드에서만 할 일이 있다: 그런 에디터에서는 도구가 낸 줄이 콘솔에 하나도 남지 않아,
    /// 「빌드 실패, 종료 코드 1」만 있고 <b>어느 파일의 무슨 오류인지가 사라진다.</b>
    /// </summary>
    void ReportFailureOutput();

    std::unique_ptr<GameEngine::Platform::IProcessRunner> mProcess;
    /// <summary>이번 단계가 낸 마지막 줄들이다. 실패했을 때에만 쓰인다.</summary>
    std::deque<std::string> mRecentOutput;
    ProjectBuildStep mStep = ProjectBuildStep::Idle;
    EditorBuildTree mTree;
    std::filesystem::path mCMakePath;
    std::filesystem::path mProjectRoot;
    std::string mTargetName;
};

}
