#include "Rules/EditorMenuModel.h"

#include <array>

namespace GameEditor
{

namespace
{
    /// <summary>
    /// 구분선이다. 이름이 비어 있는 항목이 곧 선이므로, 값 하나로 족하다.
    /// </summary>
    constexpr EditorMenuItem Separator{};

    // 실제로 도는 단축키는 셋뿐이다 — Ctrl+Z, Ctrl+Y, Ctrl+S. 나머지 항목의 칸은 비어 있고,
    // 그 비어 있음이 "아직 없다"는 정직한 답이다.
    constexpr std::array<EditorMenuItem, 12> FileItems{ {
        { "New Project", EditorMenuCommand::NewProject, {}, false },
        { "Open Project", EditorMenuCommand::OpenProject, {}, false },
        Separator,
        { "New Scene", EditorMenuCommand::NewScene, {}, false },
        { "Save Scene", EditorMenuCommand::SaveScene, "Ctrl+S", false },
        { "Rename Scene", EditorMenuCommand::RenameScene, {}, true },
        { "Delete Scene", EditorMenuCommand::DeleteScene, {}, true },
        Separator,
        { "Build", EditorMenuCommand::Build, {}, false },
        // 그만두기가 시작하기 바로 아래에 있다. 둘 중 하나만 활성이므로 같은 자리에서 둘을
        // 찾되, 지금 무엇이 되는지는 회색이 말한다.
        { "Cancel Build", EditorMenuCommand::CancelBuild, {}, false },
        Separator,
        // 말줄임을 붙이지 않는다. 저장되지 않은 편집이 있으면 닫기 경로가 묻지만, 그 물음은
        // 이 항목의 성질이 아니라 그때의 상태다 — 깨끗한 편집기에서는 아무것도 묻지 않고 닫힌다.
        { "Exit", EditorMenuCommand::Exit, {}, false },
    } };

    constexpr std::array<EditorMenuItem, 2> EditItems{ {
        { "Undo", EditorMenuCommand::Undo, "Ctrl+Z", false },
        { "Redo", EditorMenuCommand::Redo, "Ctrl+Y", false },
    } };

    constexpr std::array<EditorMenuItem, 2> AssetItems{ {
        { "New Script", EditorMenuCommand::NewScript, {}, true },
        { "New Material", EditorMenuCommand::NewMaterial, {}, true },
    } };

    constexpr std::array<EditorMenuDefinition, EditorMenuCount> Menus{ {
        { "File", FileItems },
        { "Edit", EditItems },
        { "Assets", AssetItems },
    } };
}

std::span<const EditorMenuDefinition> GetEditorMenus()
{
    return Menus;
}

bool IsEditorMenuCommandEnabled(
    const EditorMenuCommand command, const MenuCommandAvailability& availability)
{
    switch (command)
    {
    // 프로젝트를 여는 두 가지는 언제나 할 수 있다 — 아무것도 열려 있지 않을 때 할 수 있는
    // 유일한 일이 그것이다.
    case EditorMenuCommand::NewProject:
    case EditorMenuCommand::OpenProject:
        return true;

    // 닫는 것도 언제나 할 수 있다. 저장되지 않은 편집이 있으면 닫기 경로가 묻지만, 그것은
    // 항목을 회색으로 만들 이유가 아니다 — 나갈 수 없는 편집기를 만드는 셈이 된다.
    case EditorMenuCommand::Exit:
        return true;

    // 나머지는 프로젝트가 있어야 뜻이 있다.
    case EditorMenuCommand::NewScene:
    case EditorMenuCommand::NewScript:
    case EditorMenuCommand::NewMaterial:
        return availability.hasProject;

    // 시작하기와 그만두기는 서로의 반대다. 도는 동안 Build를 눌러도 될 일이 없고, 돌지 않는데
    // 그만둘 것도 없다. 둘이 같은 상태를 반대로 읽으므로 한쪽만 고쳐지는 일이 생기지 않는다.
    case EditorMenuCommand::Build:
        return availability.hasProject && !availability.isBuilding;
    case EditorMenuCommand::CancelBuild:
        return availability.isBuilding;

    // 저장은 편집 중인 장면의 것이다. 열린 것이 없으면 저장할 것도 없다.
    case EditorMenuCommand::SaveScene:
        return availability.hasProject && availability.hasOpenScene;

    // 이름 바꾸기와 지우기의 대상은 <b>고른</b> 장면이지 열린 장면이 아니다. 지우려고 먼저
    // 열어야 한다면, 지우는 순간 편집 중인 장면이 사라진다.
    case EditorMenuCommand::RenameScene:
    case EditorMenuCommand::DeleteScene:
        return availability.hasProject && availability.hasSelectedScene;

    case EditorMenuCommand::Undo:
        return availability.canUndo;
    case EditorMenuCommand::Redo:
        return availability.canRedo;
    }
    return false;
}

}
