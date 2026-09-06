#pragma once

#include <utility>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "ProjectSettings.h"
#include "../Platform/IContentSource.h"

namespace GameEngine::App
{

/// <summary>읽어 들인 프로젝트 서술자이다: 파일이 있던 곳과, 그것이 담고 있던 설정.</summary>
struct ProjectFileData
{
    std::filesystem::path filePath;
    ProjectSettings settings;

    /// <summary>
    /// 이 프로젝트의 에셋이 사는 디렉터리다. 장면 경로도 사이드카도 스키마도 이 자리 기준이다.
    ///
    /// <c>.gameproject</c>가 있는 디렉터리이고, <b>그 자리뿐이다</b> — 물을 수 있는 값이
    /// 아니다. 출하되는 <c>.gameproject</c>는 콘텐츠 루트 안에 있어야 하며,
    /// <c>sourceRootPath</c>는 코드의 자리만 별도로 지정한다.
    ///
    /// 이 답이 여기 하나뿐인 것이 요점이다. 부르는 쪽마다 <c>filePath.parent_path()</c>를 적으면
    /// 「에셋이 어디 있는가」에 답하는 자리가 부르는 쪽 수만큼 생기고, 그중 하나가 빠진 날
    /// 화면에서는 <b>어떤 도구에서는 열리고 어떤 도구에서는 없는 프로젝트</b>가 된다.
    /// </summary>
    [[nodiscard]] std::filesystem::path GetAssetRootPath() const
    {
        return filePath.parent_path();
    }

    /// <summary>
    /// 이 프로젝트의 <b>코드</b>가 사는 디렉터리다. 빌드가 CMake에게 넘기는 소스 디렉터리가
    /// 이것이며, <see cref="GetAssetRootPath"/>와 같을 수도 다를 수도 있다.
    ///
    /// 읽는 곳은 편집기의 빌드 하나뿐이다. 에셋 데이터베이스도 플레이어도 패키징도 이 값을
    /// 보지 않는다 — 출하되는 것은 콘텐츠이지 코드가 아니고, 그 셋이 보는 것은 콘텐츠 안의
    /// <c>.gameproject</c>다.
    /// </summary>
    [[nodiscard]] std::filesystem::path GetSourceRootPath() const
    {
        return Resolve(settings.sourceRootPath);
    }

private:
    /// <summary>
    /// 프로젝트 파일 기준의 상대 경로 하나를 푼다. 비어 있는 것과 <c>"."</c>을 여기서 함께
    /// 받는 이유는, 적지 않은 것과 점 하나를 적은 것이 사람에게 같은 뜻이기 때문이다.
    /// </summary>
    [[nodiscard]] std::filesystem::path Resolve(const std::filesystem::path& relative) const
    {
        const std::filesystem::path base = filePath.parent_path();
        if (relative.empty() || relative.lexically_normal() == ".")
        {
            return base;
        }
        const std::filesystem::path resolved = (base / relative).lexically_normal();
        // ".."로 끝난 경로를 정규화하면 끝에 구분자가 남는다("C:/a/b/.." → "C:/a/"). 그 자리에
        // 이름 없는 마지막 조각이 하나 생기므로, 같은 디렉터리를 가리키는 두 경로가 문자열로
        // 달라진다 — 비교하는 쪽이 그것을 알아야 한다면 이 함수가 답을 다 준 것이 아니다.
        if (resolved.has_filename())
        {
            return resolved;
        }
        return resolved.parent_path();
    }
};

/// <summary>프로젝트 디렉터리와 .gameproject 설정 파일 사이의 공통 규약을 제공한다.</summary>
class ProjectFile final
{
public:
    static constexpr std::wstring_view Extension = L".gameproject";

    /// <summary>지정한 디렉터리 바로 아래의 유일한 프로젝트 파일을 찾는다.</summary>
    [[nodiscard]] static std::optional<std::filesystem::path> FindInDirectory(
        const std::filesystem::path& directoryPath);

    /// <summary>
    /// 이 프로젝트가 콘텐츠를 두는 디렉터리다.
    ///
    /// 자리를 정하는 것은 이름이 아니라 <c>.gameproject</c>가 있는 곳이다. 흔한 모양은
    /// <c>Content/</c> 아래지만, 어떤 프로젝트는 자기 루트에 그것과 함께 둔다 — 프로젝트는 자기
    /// 저장소이고 자기 모양을 스스로 정하며, CMake 쪽도 <c>CONTENT_DIR</c>로 이미 둘 다 받는다.
    ///
    /// 그래서 이 답이 한 곳에 있다. 도구마다 「Content/일 것이다」를 따로 적어 두면, 루트에 둔
    /// 프로젝트는 어떤 도구에서는 열리고 어떤 도구에서는 없는 것이 된다.
    /// </summary>
    /// <param name="projectDirectory">프로젝트 디렉터리다.</param>
    /// <returns>콘텐츠 루트이며, 어느 자리에도 프로젝트 파일이 없으면 비어 있다.</returns>
    [[nodiscard]] static std::filesystem::path FindContentRoot(
        const std::filesystem::path& projectDirectory);

    /// <summary>
    /// 콘텐츠 소스가 담은 단 하나의 루트 `.gameproject`이다. 소스 기준 상대 경로다. 플레이어가
    /// 자기가 실행하도록 빌드된 게임을 찾는 방법이 이것이다 — 그 게임이 실행 파일 옆의 디렉터리에
    /// 있든 실행 파일 안에 있든.
    /// </summary>
    [[nodiscard]] static std::optional<std::filesystem::path> FindInSource(
        const Platform::IContentSource& source);

    /// <summary>확장자, 파일명과 projectName 일치 여부 및 설정 내용을 검증해 읽는다.</summary>
    [[nodiscard]] static std::optional<ProjectFileData> Load(
        const std::filesystem::path& filePath);

    /// <summary>콘텐츠 소스가 담은 서술자를 읽고 검증한다.</summary>
    [[nodiscard]] static std::optional<ProjectFileData> Load(
        const Platform::IContentSource& source, const std::filesystem::path& relativePath);

    /// <summary>
    /// 새 장면에 줄 ID다: 아직 쓰이지 않은 가장 작은 값이다.
    ///
    /// 빈 자리를 메우는 이유는 ID가 이름이 아니라 자리이기 때문이다. 장면을 지우고 새로 만들면
    /// 번호가 끝없이 자라는 대신 지운 자리가 다시 쓰인다. 겹치는 ID를 내주지 않는 것이 이
    /// 함수의 계약이다 — 겹치면 같은 번호의 두 장면 중 어느 쪽이 열리는지 아무도 모른다.
    /// </summary>
    [[nodiscard]] static unsigned int ChooseSceneId(const ProjectSettings& settings);

    /// <summary>
    /// 장면 파일 하나의 내용이다. 카메라 하나가 들어 있다.
    ///
    /// 비어 있지 않은 이유는, 빈 장면을 열면 화면에 아무것도 나오지 않아 사람이 그것을 고장으로
    /// 읽기 때문이다. 새 프로젝트의 기본 장면도 같은 이유로 카메라를 갖는다.
    /// </summary>
    /// <param name="sceneName">장면 파일 안에 적힐 이름이다.</param>
    [[nodiscard]] static std::string MakeSceneContents(std::string_view sceneName);

    /// <summary>
    /// 설정을 .gameproject 파일의 내용으로 적는다. 읽는 쪽과 같은 낱말을 쓴다.
    ///
    /// 이 함수가 아는 열쇠만 적히므로, 엔진이 모르는 열쇠가 파일에 있었다면 남지 않는다.
    /// </summary>
    [[nodiscard]] static std::string Serialize(const ProjectSettings& settings);

    /// <summary>
    /// 장면 하나를 더한 설정이다. 그 경로가 이미 등록돼 있으면 실패한다 — 같은 파일을 두 ID로
    /// 등록하면 계층에 같은 장면이 두 줄로 서게 된다.
    /// </summary>
    /// <param name="relativeScenePath">프로젝트 루트 기준 경로다.</param>
    /// <param name="sceneId">배정할 ID다. 이미 쓰이고 있으면 실패한다.</param>
    [[nodiscard]] static std::optional<ProjectSettings> WithScene(
        const ProjectSettings& settings, const std::filesystem::path& relativeScenePath,
        unsigned int sceneId);

    /// <summary>
    /// 새 장면 파일을 만들고 프로젝트에 등록한다. 파일을 만드는 것만으로는 부족하다 — 계층이
    /// 읽는 것은 디렉터리가 아니라 .gameproject의 scenes 배열이라, 등록하지 않은 장면은
    /// 만들어도 어디에도 나타나지 않는다.
    /// </summary>
    /// <param name="project">열려 있는 프로젝트다.</param>
    /// <param name="sceneFilePath">만들 장면 파일의 경로다. 프로젝트 폴더 안이어야 한다.</param>
    /// <returns>갱신된 프로젝트다. 새 장면의 ID는 그 scenePaths에서 경로로 찾을 수 있다.</returns>
    [[nodiscard]] static std::optional<ProjectFileData> AddScene(
        const ProjectFileData& project, const std::filesystem::path& sceneFilePath);

    /// <summary>새 프로젝트 파일과 기본 장면을 생성하고 다시 읽어 검증한다.</summary>
    [[nodiscard]] static std::optional<ProjectFileData> Create(
        const std::filesystem::path& filePath);

    /// <summary>
    /// 새 프로젝트가 콘텐츠를 두는 자리다. 빌드 스크립트의 <c>CONTENT_DIR</c>이 이 값에서
    /// 나온다 — 새 프로젝트는 언제나 프로젝트 파일과 콘텐츠가 한 디렉터리이므로 "."이다.
    /// </summary>
    static constexpr std::string_view DefaultContentDirectory = ".";

    /// <summary>
    /// 이름을 CMake 타깃과 C++ 네임스페이스로 쓸 수 있는지다: 문자나 밑줄로 시작하고 그 뒤는
    /// 문자·숫자·밑줄이며, C++ 예약어가 아니어야 한다.
    ///
    /// 프로젝트 이름과 컴포넌트 이름이 둘 다 그 두 자리에 놓이므로 판정이 하나다. 공백이나 한글이
    /// 든 이름은 게임으로서는 멀쩡하지만 빌드 스크립트에는 쓸 수 없고, 그때는 조용히 고쳐 쓰는
    /// 대신 이유를 말하고 거절한다 — 사람이 지은 이름을 도구가 바꿔 두면 나중에 그 이름을 찾는
    /// 사람이 없는 것을 찾는다.
    /// </summary>
    [[nodiscard]] static bool IsUsableAsIdentifier(std::string_view name);

    /// <summary>
    /// 프로젝트 루트에 빌드 스크립트를 놓는다. 이미 있으면 아무것도 하지 않고 참이다.
    ///
    /// 세 줄인 이유는 프로젝트가 엔진 빌드 안에서 평가되기 때문이다: 엔진 경로를 적을 필요가
    /// 없고, 소스는 목록이 아니라 glob이라 파일이 늘어도 이 파일은 그대로다.
    /// </summary>
    /// <param name="projectRoot">.gameproject가 있는 디렉터리다.</param>
    /// <param name="projectName">CMake 타깃이 될 이름이다. 식별자여야 한다.</param>
    /// <param name="contentDirectory">서술자가 말한 콘텐츠 자리다. 그대로 CONTENT_DIR가 된다.</param>
    /// <param name="iconPath">
    /// 아이콘 에셋의 실제 파일 자리다. 비어 있으면 프로젝트에 아이콘이 없다는 뜻이고,
    /// 생성되는 스크립트도 ICON을 쓰지 않는다. guid를 실제 경로로 푸는 일은 부르는 쪽이
    /// 한다 — 이 함수는 이미 풀린 값을 받아 적을 뿐, 살아있는 에셋 데이터베이스를 모른다.
    /// </param>
    /// <param name="iconGuid">
    /// 그 아이콘 에셋의 guid다. <paramref name="iconPath"/>가 있을 때만 뜻이 있다. 함께
    /// 적으면 매크로 자신이 그 경로의 <c>.meta</c>와 대조해, 둘 중 하나가 낡았을 때
    /// configure가 실패하며 무엇이 어긋났는지 말한다.
    /// </param>
    [[nodiscard]] static bool WriteBuildScript(
        const std::filesystem::path& projectRoot, std::string_view projectName,
        const std::filesystem::path& contentDirectory,
        const std::filesystem::path& iconPath = {}, std::string_view iconGuid = {});

    /// <summary>
    /// 장면 하나를 뺀 설정이다. 그 ID가 없으면 실패한다.
    ///
    /// 마지막 장면은 뺄 수 없다. 장면이 없는 프로젝트는 <c>IsValid</c>가 거부하므로 열리지
    /// 않는다 — 지우는 동작이 프로젝트를 못 여는 상태로 만들어서는 안 된다.
    ///
    /// 시작 장면을 빼면 <c>initialSceneId</c>는 남은 것 중 가장 작은 ID로 옮겨 간다. 그대로 두면
    /// 없는 장면을 가리켜 역시 열리지 않기 때문이다. 조용히 바뀌는 값이므로 부르는 쪽이 사람에게
    /// 알려야 한다.
    /// </summary>
    [[nodiscard]] static std::optional<ProjectSettings> WithoutScene(
        const ProjectSettings& settings, unsigned int sceneId);

    /// <summary>
    /// 장면 하나의 등록 경로를 바꾼 설정이다. ID는 유지된다 — 바꾸면 <c>initialSceneId</c>가
    /// 딸려 와야 하고, 이름을 고치는 일이 시작 장면을 옮기는 일이 되어서는 안 된다.
    /// </summary>
    [[nodiscard]] static std::optional<ProjectSettings> WithRenamedScene(
        const ProjectSettings& settings, unsigned int sceneId,
        const std::filesystem::path& newRelativePath);

    /// <summary>
    /// 장면 파일과 그 등록을 함께 지운다. 되돌릴 수 없다.
    ///
    /// <b>등록을 먼저 지우고 파일을 나중에 지운다.</b> 파일부터 지우면 중간 실패 시 등록이 없는
    /// 파일을 가리켜 장면을 열 수 없다. 등록부터 지우면 실패하더라도 주인 없는 파일만 남고,
    /// 그 파일은 다시 등록할 수 있다.
    /// </summary>
    /// <param name="project">열려 있는 프로젝트다.</param>
    /// <param name="sceneId">지울 장면의 ID다.</param>
    [[nodiscard]] static std::optional<ProjectFileData> RemoveScene(
        const ProjectFileData& project, unsigned int sceneId);

    /// <summary>
    /// 장면 파일의 이름과 그 등록 경로를 함께 바꾼다.
    ///
    /// <b>파일을 먼저 옮기고 등록을 나중에 고친다.</b> 여기서는 이 순서가 안전하다: 옮기다
    /// 실패하면 아무것도 바뀌지 않았고, 옮긴 뒤 등록에 실패하면 남는 것은 "없는 파일을 가리키는
    /// 등록"이라 위험해 보이지만, 그 경우 우리는 파일을 제자리로 되돌릴 수 있다 — 지우기와 달리
    /// 원본이 아직 존재하기 때문이다.
    /// </summary>
    /// <param name="sceneFilePath">새 파일 경로다. 프로젝트 폴더 안이어야 한다.</param>
    [[nodiscard]] static std::optional<ProjectFileData> RenameScene(
        const ProjectFileData& project, unsigned int sceneId,
        const std::filesystem::path& sceneFilePath);

    /// <summary>
    /// 등록된 장면 파일이 실제로 있는지다. 등록이 진실이고 파일은 그림자라, 파일만 지워도 등록은
    /// 남는다 — 그 상태를 사람에게 보이려면 물어볼 곳이 있어야 한다.
    /// </summary>
    [[nodiscard]] static bool SceneFileExists(
        const ProjectFileData& project, unsigned int sceneId);

    [[nodiscard]] static bool HasProjectExtension(const std::filesystem::path& filePath);
};

}
