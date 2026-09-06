#pragma once

#include <filesystem>
#include <vector>

namespace GameEngine::Build
{

/// <summary>
/// 프로젝트의 파일들을 플레이어 실행 파일 사본 뒤에 덧붙여, 자기 완결적인 파일 하나를 만든다.
///
/// 컴파일의 일부가 아니라 빌드의 마지막 단계이며, 그래서 값이 싸다: 실행 파일 자신의 바이트는
/// 이미 최종이라, 콘텐츠 변경은 덧붙인 블록만 다시 쓰고 아무것도 다시 링크하지 않는다.
///
/// `relativePaths`는 엔진이 요청할 그대로의 이름이라서, 빌드가 패킹하는 것과 실행 중인 게임이
/// 조회하는 것이 같은 문자열이다.
/// </summary>
[[nodiscard]] bool AppendContentPack(
    const std::filesystem::path& executablePath,
    const std::filesystem::path& contentRootPath,
    const std::vector<std::filesystem::path>& relativePaths);

}
