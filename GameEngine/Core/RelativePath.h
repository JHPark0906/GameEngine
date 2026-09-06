#pragma once

#include <filesystem>
#include <optional>

namespace GameEngine::Core
{

/// <summary>
/// 이 상대 경로가 자기가 놓인 자리 밖으로 나가는지다.
///
/// 구성 요소 중 하나라도 <c>..</c>이면 나간다. <b>첫 번째만</b> 보는 것으로는 모자란다 —
/// <c>Scenes/../../outside</c>는 첫 요소가 <c>..</c>이 아니지만 두 단계 위를 가리킨다.
/// 절대 경로도 나간 것으로 본다. 그것은 자기가 놓인 자리를 아예 무시하는 경로다.
///
/// 경로를 <b>적힌 그대로</b> 본다. 다듬고 나서 보지 않으므로 <c>a/../b</c>도 거절한다 — 결국
/// 안에 남는 경로지만, 이 함수를 부르는 자리들은 밖에서 받은 경로를 그대로 의심하는 자리고,
/// 그런 입력을 먼저 다듬어 주는 것은 검사를 느슨하게 만드는 일이다. 다듬은 뒤에 묻는 것은
/// <see cref="RelativePathWithin"/>이고, 두 답이 다른 것이 규칙이다.
/// </summary>
[[nodiscard]] bool EscapesRoot(const std::filesystem::path& relativePath);

/// <summary>
/// 이 경로를 그 루트 기준의 상대 경로로 옮긴다. 루트 밖이면 값이 없다.
///
/// 루트 밖 경로를 거부하는 규칙을 공유해, 호출 지점에 따라 경로 허용 범위가 달라지지 않게 한다.
///
/// 루트를 인자로 받는 이유는 부르는 쪽마다 그것이 다르기 때문이다: 에셋 데이터베이스에게는
/// 프로젝트 루트이고, 콘텐츠 소스에게는 자기 루트이며, 장면 등록에게는 <c>.gameproject</c>가
/// 있는 디렉터리다. 「프로젝트」를 이름에 넣지 않은 것도 그래서다.
///
/// 결과는 슬래시 형식이다. 이 값은 곧 파일에 적히거나 조회 키가 되는데, 같은 경로가 자리에
/// 따라 <c>Scenes/Main</c>과 <c>Scenes\Main</c> 두 글자가 되면 그 둘은 같은 것을 가리키면서도
/// 서로 다른 키가 된다.
/// </summary>
/// <param name="root">기준이 되는 디렉터리다. 비어 있으면 절대 경로를 옮길 수 없다.</param>
/// <param name="path">옮길 경로다. 상대 경로면 이미 루트 기준인 것으로 본다.</param>
/// <returns>루트 기준 상대 경로이며, 비었거나 루트 밖이면 값이 없다.</returns>
[[nodiscard]] std::optional<std::filesystem::path> RelativePathWithin(
    const std::filesystem::path& root, const std::filesystem::path& path);

}
