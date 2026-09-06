#pragma once

// editor-layer: 0 (Rules)

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace GameEditor
{

/// <summary>컴포넌트 하나를 만들면서 놓인 파일들이다.</summary>
struct CreatedScript
{
    /// <summary>선언이 놓인 자리다.</summary>
    std::filesystem::path headerPath;
    /// <summary>정의와 등록이 놓인 자리다.</summary>
    std::filesystem::path sourcePath;
    /// <summary>이 프로젝트에 빌드 스크립트가 없어서 함께 놓았는지다.</summary>
    bool wroteBuildScript = false;
};

/// <summary>
/// 열린 프로젝트에 컴포넌트 하나를 만든다: <c>Source/&lt;Name&gt;.h</c>와 그 <c>.cpp</c>.
///
/// 프로젝트가 코드를 처음 갖는 순간이 이 함수다. 그래서 <c>Source/</c>가 없으면 만들고, 빌드
/// 스크립트가 없으면 그것도 함께 놓는다 — 콘텐츠만 있던 프로젝트(자기 저장소에 장면과 그림만
/// 두고 있던 것)가 여기서 빌드되는 프로젝트가 된다. 이미 있는 스크립트는 건드리지 않는다:
/// 소스를 glob으로 모으므로 파일이 하나 늘었다고 고칠 것이 없다.
///
/// 만들어진 컴포넌트는 그 자리에서 도는 것이 아니다. 에디터가 프로젝트의 오브젝트를 링크해
/// 갖고 있으므로, 새 컴포넌트가 Add Component 목록에 나오려면 configure와 에디터 재빌드가
/// 필요하다. 그 사실을 호출자가 사람에게 말해야 한다.
/// </summary>
/// <param name="projectRoot">.gameproject가 있는 디렉터리다.</param>
/// <param name="projectName">컴포넌트를 담을 네임스페이스이자 CMake 타깃 이름이다.</param>
/// <param name="contentDirectory">서술자가 말한 콘텐츠 자리다. 빌드 스크립트를 쓸 때만 쓰인다.</param>
/// <param name="iconPath">
/// 프로젝트가 이미 아이콘을 정해 뒀다면 그 실제 파일 자리다(비어 있으면 아이콘 없음). 빌드
/// 스크립트를 쓸 때만 쓰인다 — guid를 이 경로로 푸는 일은 부르는 쪽이 산 에셋 데이터베이스로
/// 이미 해 뒀다.
/// </param>
/// <param name="iconGuid">그 아이콘 에셋의 guid다. iconPath가 있을 때만 뜻이 있다.</param>
/// <param name="chosenHeaderPath">
/// 사람이 고른 자리다. 파일 이름의 몸통이 컴포넌트 이름이 되고, 그 디렉터리가 파일이 놓일
/// 자리다 — 고른 곳과 다른 곳에 만들지 않는다.
///
/// 그래서 <c>Source/</c> 바로 아래가 아니면 거절한다. 빌드 스크립트가 그 디렉터리 하나만 훑기
/// 때문이며(재귀가 아니라 하위 폴더도 들지 않는다), 다른 자리에 놓인 파일은 어떤 빌드에도
/// 들어가지 않는다. 조용히 옮기는 대신 이유를 말하는 이유는 이름을 거절할 때와 같다: 사람이
/// 고른 것을 도구가 바꿔 두면 나중에 그 자리를 찾는 사람이 없는 것을 찾는다.
/// </param>
/// <returns>놓인 파일들이다. 이름이나 자리가 쓸 수 없거나 이미 있으면 비어 있다.</returns>
[[nodiscard]] std::optional<CreatedScript> CreateComponentScript(
    const std::filesystem::path& projectRoot, std::string_view projectName,
    const std::filesystem::path& contentDirectory,
    const std::filesystem::path& chosenHeaderPath,
    const std::filesystem::path& iconPath = {}, std::string_view iconGuid = {});

}
