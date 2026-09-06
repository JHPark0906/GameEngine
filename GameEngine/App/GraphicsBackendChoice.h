#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ProjectSettings.h"

namespace GameEngine::App
{

/// <summary>플레이어가 시작할 때 고를 수 있는 그래픽 백엔드 하나다.</summary>
struct GraphicsBackendChoice
{
    /// <summary>백엔드 레지스트리의 식별자다. 예: "D3D12".</summary>
    std::string id;
    /// <summary>실행 중인 머신이 이 백엔드의 장치를 만들 수 있는지다.</summary>
    bool supported = true;
};

/// <summary>
/// 이 빌드에 컴파일된 백엔드마다 하나씩, 레지스트리의 순서대로다. 자동 선택은 백엔드가
/// 아니므로 들어 있지 않다 — 사람은 실제 백엔드 가운데 하나를 고른다.
/// </summary>
[[nodiscard]] std::vector<GraphicsBackendChoice> ListGraphicsBackendChoices();

/// <summary>선택지의 버튼 라벨이다. 이 머신이 지원하지 않는 백엔드는 라벨에 그 사실이 붙는다.</summary>
[[nodiscard]] std::string DescribeGraphicsBackendChoice(const GraphicsBackendChoice& choice);

/// <summary>
/// 시작할 때마다 사람에게 묻겠다는 설정 값이다.
///
/// 백엔드 식별자가 아니라 선택 정책이며, 그래서 백엔드 레지스트리가 아니라 여기에 산다:
/// 정책 값은 그것을 푸는 층에 속하고, 자동 선택(<c>Rendering::AutomaticGraphicsBackendId</c>)은
/// 장치 팩토리가 풀지만 이것은 대화상자를 띄우는 App이 푼다. 팩토리에는 사람이 고른 실제
/// 식별자만 도달하므로 이 값은 그 앞에서 사라진다.
/// </summary>
inline constexpr std::string_view SelectGraphicsBackendId = "Select";

/// <summary>설정 값이 시작할 때 묻기를 요구하는지 여부이다.</summary>
[[nodiscard]] bool RequestsGraphicsBackendDialog(std::string_view id);

/// <summary>설정 값이 무엇을 뜻하는지 한 줄로 적는다. 로그가 정책과 식별자를 구별하게 한다.</summary>
[[nodiscard]] std::string DescribeGraphicsBackendSetting(std::string_view id);

/// <summary>
/// 설정 파일의 백엔드 항목에 적을 수 있는 값 전부다. 이 빌드에 컴파일된 백엔드가 레지스트리
/// 순서로 먼저 오고, 그 뒤에 정책 값 둘이 온다.
///
/// 설정 파일에 함께 적히라고 있는 것이다: 사람이 파일을 열었을 때 무엇을 적을 수 있는지가
/// 보이지 않으면, 값을 바꾸려면 코드를 읽는 수밖에 없다.
/// </summary>
[[nodiscard]] std::vector<std::string> ListGraphicsBackendSettingValues();

/// <summary>
/// 대화상자의 결과를 프로젝트 설정에 적용한다. 고른 선택지의 식별자가 요청 백엔드가 된다.
/// </summary>
/// <param name="chosen">눌린 선택지의 인덱스이며, 취소는 값 없음이다.</param>
/// <returns>설정이 바뀌어 게임을 열어도 되면 true, 취소나 범위 밖 인덱스면 설정을 건드리지 않고 false다.</returns>
[[nodiscard]] bool ApplyGraphicsBackendChoice(
    ProjectSettings& settings,
    std::span<const GraphicsBackendChoice> choices,
    std::optional<std::size_t> chosen);

/// <summary>대화상자 없이 백엔드를 정하는 명령줄 스위치다. 다음 인수가 식별자다.</summary>
inline constexpr std::string_view GraphicsBackendOption = "--graphics-backend";

/// <summary>
/// 명령줄이 백엔드를 정했으면 그 식별자다. 스위치가 없으면 값이 없고, 스위치 뒤에 식별자가
/// 없으면 빈 문자열이다.
/// </summary>
[[nodiscard]] std::optional<std::string> FindGraphicsBackendArgument(
    std::span<const std::string> arguments);

/// <summary>명령줄이 정한 식별자를 설정에 적용한다. 레지스트리가 모르는 식별자나 빈 식별자는 거절한다.</summary>
[[nodiscard]] bool ApplyRequestedGraphicsBackend(ProjectSettings& settings, std::string_view id);

}
