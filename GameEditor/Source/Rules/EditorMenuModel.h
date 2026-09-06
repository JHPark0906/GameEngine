#pragma once

// editor-layer: 0 (Rules)

#include <cstddef>
#include <span>
#include <string_view>

namespace GameEditor
{

/// <summary>
/// 메뉴 항목이 부르는 명령이다. 이름만 있고 동작은 없다 — 무엇을 하는지는 셸이 알고, 이 표는
/// 무엇이 어느 메뉴에 어떤 이름으로 놓이는지만 안다.
/// </summary>
enum class EditorMenuCommand
{
    NewProject,
    OpenProject,
    NewScene,
    SaveScene,
    RenameScene,
    DeleteScene,
    Build,
    Undo,
    Redo,
    NewScript,
    NewMaterial,

    /// <summary>
    /// 도는 빌드를 그만두게 한다.
    ///
    /// 메뉴 표가 고정된 이름과 위치를 설명하도록 Build와 CancelBuild를 별도 항목으로 둔다.
    ///
    /// 둘 중 하나만 활성이다. 도는 동안 Build가 꺼지고 이것이 켜지므로, 같은 자리에서 시작과
    /// 그만두기를 찾되 지금 무엇이 되는지는 회색이 말한다.
    /// </summary>
    CancelBuild,

    /// <summary>
    /// 에디터를 닫는다.
    ///
    /// 이 항목은 확인 UI를 직접 만들지 않는다. bootstrap이 창의 X 단추와 같은
    /// 종료 판단을 거친 뒤 승인된 경우에만 창을 닫는다.
    /// </summary>
    Exit,
};

/// <summary>메뉴 막대의 머리줄 하나다. 순서가 곧 화면의 왼쪽부터의 순서다.</summary>
enum class EditorMenu : std::size_t
{
    File,
    Edit,
    Assets,
};
inline constexpr std::size_t EditorMenuCount = 3;

/// <summary>
/// 펼친 목록의 한 줄이다.
///
/// <c>label</c>이 비어 있으면 <b>구분선</b>이며, 그때 나머지 필드는 의미가 없다. 구분선을
/// 항목의 한 종류로 두는 이유는 그것이 순서의 일부이기 때문이다 — 어디에 선이 들어가는지는
/// 항목 목록과 떨어져서는 말할 수 없다.
/// </summary>
struct EditorMenuItem
{
    std::string_view label;
    EditorMenuCommand command = EditorMenuCommand::NewProject;

    /// <summary>
    /// 오른쪽에 흐리게 적히는 단축키다. <b>실제로 도는 것만</b> 적는다 — 비어 있으면 안 적는다.
    ///
    /// 메뉴에 적힌 글자는 약속이고, 눌러도 아무 일이 없는 약속은 기능이 없는 것보다 나쁘다.
    /// 그래서 이 칸을 채우는 조건은 "그 키가 실제로 처리된다"이며, 시험이 그것을 지킨다.
    /// </summary>
    std::string_view shortcut;

    /// <summary>
    /// 누르는 즉시 일어나지 않고 한 번 더 묻는 항목인지다. 참이면 이름 끝에 말줄임이 붙는다.
    ///
    /// 저장되지 않은 편집 때문에 물을 수 있는 것들은 여기 들지 않는다 — 그 물음은 항목의
    /// 성질이 아니라 그때의 상태다.
    /// </summary>
    bool asks = false;

    [[nodiscard]] bool IsSeparator() const { return label.empty(); }
};

/// <summary>메뉴 하나의 이름과 그 항목들이다.</summary>
struct EditorMenuDefinition
{
    std::string_view title;
    std::span<const EditorMenuItem> items;
};

/// <summary>
/// 메뉴 막대의 표다. 이 표가 화면의 유일한 근거이며, 코드 어디에도 두 번째 목록이 없다.
/// </summary>
[[nodiscard]] std::span<const EditorMenuDefinition> GetEditorMenus();

/// <summary>
/// 명령들이 지금 고를 수 있는지를 정하는 편집기의 상태다. 메뉴는 이것을 판단하지 않고 받는다 —
/// 무엇이 열려 있고 무엇이 도는지 아는 곳은 문서 모델과 빌드다.
///
/// 깃발들을 구조체로 묶어 두는 것은 이름으로 넘기기 위해서다. 낱개의 <c>bool</c> 여섯을
/// 자리순으로 넘기면 두 개를 맞바꿔도 컴파일러가 아무 말을 하지 않고, 그 실수는 「엉뚱한
/// 항목이 회색이다」로만 나타난다.
/// </summary>
struct MenuCommandAvailability
{
    bool hasProject = false;
    bool hasOpenScene = false;
    bool hasSelectedScene = false;
    bool canUndo = false;
    bool canRedo = false;

    /// <summary>지금 프로젝트 빌드가 돌고 있는지다. Build와 Cancel Build를 가른다.</summary>
    bool isBuilding = false;
};

/// <summary>
/// 이 명령이 지금 고를 수 있는지다. 고를 수 없는 항목은 보이되 눌리지 않는다 — 사라지면
/// 사람이 그 기능을 찾다가 없다고 결론짓고, 회색으로 남으면 "지금은 아니다"를 읽는다.
///
/// 이 답이 <b>한 곳</b>인 것이 이 함수의 전부다. 항목마다 술어를 따로 들려 보내면 같은 물음에
/// 답하는 자리가 항목 수만큼 생기고, 그중 하나가 상태를 놓치는 날 화면에서는 「이것만 왜
/// 회색이지」로 보인다.
/// </summary>
[[nodiscard]] bool IsEditorMenuCommandEnabled(
    EditorMenuCommand command, const MenuCommandAvailability& availability);

}
