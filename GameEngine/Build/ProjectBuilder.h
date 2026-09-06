#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>

namespace GameEngine::Build
{

/// <summary>한 번의 프로젝트 패키징에 필요한 입력들이다: 콘텐츠 루트, 실행 파일, 런타임 파일, 출력 위치.</summary>
struct ProjectBuildRequest
{
    std::filesystem::path contentRootPath;
    std::filesystem::path executablePath;
    std::filesystem::path runtimeRootPath;
    std::filesystem::path outputPath;

    /// <summary>
    /// 스테이징된 콘텐츠를 실행 파일에 덧붙인다. OS 로더가 읽는 네이티브 DLL은 밖에 남긴다.
    ///
    /// 패키지는 먼저 디렉터리로 조립하고 검증한다. 단일 파일 출력은 검증된 콘텐츠를
    /// 실행 파일에 덧붙이는 단계만 추가하므로 두 출력 방식이 같은 검증을 거친다.
    /// </summary>
    bool packContentIntoExecutable = false;

    /// <summary>
    /// 정체성이 없는 에셋에게 발급해도 되는지다. 기본값은 안 된다이다.
    ///
    /// 매니페스트는 정체성 없는 에셋을 거부하므로, 편집기를 한 번도 거치지 않은 프로젝트는
    /// 이것 없이 빌드되지 않는다. 그럼에도 기본값이 꺼져 있는 이유는 발급이 <b>사용자의
    /// 프로젝트에 파일을 만들기</b> 때문이다. 빌드를 한 번 돌렸다고 저장소에 새 파일이 생기는 것은
    /// 사람이 고를 일이지 도구가 정할 일이 아니라서, 말하고 받을 때만 한다.
    /// </summary>
    bool issueMissingIdentities = false;
};

/// <summary>성공한 패키징이 보고하는 것들이다: 출력 위치와 담긴 것들의 개수.</summary>
struct ProjectBuildResult
{
    std::filesystem::path outputPath;
    std::size_t assetCount = 0;
    std::size_t sceneCount = 0;
    std::size_t runtimeFileCount = 0;

    /// <summary>콘텐츠가 실행 파일 안에 있는지다. 네이티브 DLL과 디버그 심볼은 별도 파일이다.</summary>
    bool packedIntoExecutable = false;
};

/// <summary>
/// 컴파일된 프로젝트 실행 파일과 검증된 콘텐츠를 원자적으로 배포 디렉터리에 구성한다.
/// </summary>
class ProjectBuilder final
{
public:
    /// <summary>기존 성공 결과는 실패 시 보존하며 새 프로젝트 패키지를 생성한다.</summary>
    [[nodiscard]] static std::optional<ProjectBuildResult> Build(
        const ProjectBuildRequest& request);
};

}
