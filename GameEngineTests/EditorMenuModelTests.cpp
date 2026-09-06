#include <algorithm>
#include <array>
#include <iostream>
#include <string_view>
#include <vector>

#include "Rules/EditorMenuModel.h"

#include "EditorMenuModelTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEditor::EditorMenuCommand;
    using GameEditor::EditorMenuItem;

    /// <summary>
    /// 에디터가 실제로 처리하는 단축키다. `EditorShell::HandleUndoShortcuts`가 그 셋이며,
    /// 메뉴가 적을 수 있는 것은 여기 있는 것뿐이다.
    ///
    /// 이 목록이 시험 안에 손으로 적혀 있는 것은 의도적이다. 코드에서 읽어 오면 "적힌 것이
    /// 도는 것과 같다"가 동어반복이 되고, 단축키를 지우면서 메뉴의 글자를 남기는 실수가 그대로
    /// 통과한다. 여기 적힌 셋이 늘거나 줄면 사람이 이 줄을 고치며 한 번 더 생각하게 된다.
    /// </summary>
    constexpr std::array<std::string_view, 3> WorkingShortcuts{ "Ctrl+Z", "Ctrl+Y", "Ctrl+S" };

    [[nodiscard]] std::vector<const EditorMenuItem*> AllItems()
    {
        std::vector<const EditorMenuItem*> items;
        for (const GameEditor::EditorMenuDefinition& menu : GameEditor::GetEditorMenus())
        {
            for (const EditorMenuItem& item : menu.items)
            {
                items.push_back(&item);
            }
        }
        return items;
    }
}

bool RunEditorMenuModelTests()
{
    const std::span<const GameEditor::EditorMenuDefinition> menus = GameEditor::GetEditorMenus();

    // 머리줄 셋과 그 순서. Play 메뉴는 없다 — Play·Stop은 툴바 버튼으로 남고, Build는 File이다.
    const bool headingsAreThree = menus.size() == 3 && menus[0].title == "File" &&
        menus[1].title == "Edit" && menus[2].title == "Assets";

    const std::vector<const EditorMenuItem*> items = AllItems();

    // 적힌 단축키는 전부 실제로 도는 셋 중 하나여야 한다. 없는 단축키를 적지 않는다.
    bool everyShortcutWorks = true;
    for (const EditorMenuItem* const item : items)
    {
        if (item->shortcut.empty())
        {
            continue;
        }
        const bool known = std::find(WorkingShortcuts.begin(), WorkingShortcuts.end(),
                               item->shortcut) != WorkingShortcuts.end();
        if (!known)
        {
            std::cerr << "  menu item \"" << item->label << "\" claims a shortcut that nothing "
                      << "handles: " << item->shortcut << "\n";
            everyShortcutWorks = false;
        }
    }

    // 구분선은 이름이 비어 있는 항목이고, 목록의 처음이나 끝에 오지 않는다 — 그 자리의 선은
    // 아무것도 가르지 않으면서 한 줄을 차지한다.
    bool separatorsSeparate = true;
    for (const GameEditor::EditorMenuDefinition& menu : menus)
    {
        if (menu.items.empty())
        {
            separatorsSeparate = false;
            continue;
        }
        if (menu.items.front().IsSeparator() || menu.items.back().IsSeparator())
        {
            separatorsSeparate = false;
        }
        for (std::size_t index = 1; index < menu.items.size(); ++index)
        {
            if (menu.items[index].IsSeparator() && menu.items[index - 1].IsSeparator())
            {
                separatorsSeparate = false;
            }
        }
    }

    // 이름은 저마다 하나뿐이어야 한다. 같은 이름이 두 줄이면 사람은 어느 쪽을 눌렀는지 모른다.
    std::vector<std::string_view> labels;
    for (const EditorMenuItem* const item : items)
    {
        if (!item->IsSeparator())
        {
            labels.push_back(item->label);
        }
    }
    std::sort(labels.begin(), labels.end());
    const bool labelsAreUnique = std::adjacent_find(labels.begin(), labels.end()) == labels.end();

    // 한 번 더 묻는 항목만 말줄임을 받는다. 지금 그것은 이름을 묻는 둘과 지우는 하나다.
    const auto asksToBeAsked = [&items](const std::string_view label)
    {
        for (const EditorMenuItem* const item : items)
        {
            if (item->label == label)
            {
                return item->asks;
            }
        }
        return false;
    };
    const bool asksMarked = asksToBeAsked("Rename Scene") && asksToBeAsked("Delete Scene") &&
        asksToBeAsked("New Script") && !asksToBeAsked("Save Scene") && !asksToBeAsked("Undo");

    // 아무것도 열려 있지 않을 때 할 수 있는 일은 프로젝트를 여는 둘뿐이다. 그때 나머지가
    // 눌린다면 사람은 아무 일도 일어나지 않는 버튼을 누르게 된다.
    const auto enabled = [](const EditorMenuCommand command, const bool hasProject,
                             const bool hasOpenScene, const bool hasSelectedScene)
    {
        GameEditor::MenuCommandAvailability availability;
        availability.hasProject = hasProject;
        availability.hasOpenScene = hasOpenScene;
        availability.hasSelectedScene = hasSelectedScene;
        return GameEditor::IsEditorMenuCommandEnabled(command, availability);
    };
    const bool emptyEditorOffersOnlyProjects =
        enabled(EditorMenuCommand::NewProject, false, false, false) &&
        enabled(EditorMenuCommand::OpenProject, false, false, false) &&
        !enabled(EditorMenuCommand::NewScene, false, false, false) &&
        !enabled(EditorMenuCommand::SaveScene, false, false, false) &&
        !enabled(EditorMenuCommand::Build, false, false, false);

    // 저장은 열린 장면의 것이고, 이름 바꾸기와 지우기는 고른 장면의 것이다 — 둘은 다른 조건이다.
    const bool sceneCommandsFollowTheirOwnTarget =
        enabled(EditorMenuCommand::SaveScene, true, true, false) &&
        !enabled(EditorMenuCommand::SaveScene, true, false, true) &&
        enabled(EditorMenuCommand::DeleteScene, true, false, true) &&
        !enabled(EditorMenuCommand::DeleteScene, true, true, false);

    // undo/redo는 스택이 답한다. 프로젝트가 없어도 스택이 비어 있으면 같은 답이다.
    const auto withStacks = [](const bool canUndo, const bool canRedo)
    {
        GameEditor::MenuCommandAvailability availability;
        availability.hasProject = true;
        availability.hasOpenScene = true;
        availability.hasSelectedScene = true;
        availability.canUndo = canUndo;
        availability.canRedo = canRedo;
        return availability;
    };
    const bool undoFollowsTheStack =
        GameEditor::IsEditorMenuCommandEnabled(EditorMenuCommand::Undo, withStacks(true, false)) &&
        !GameEditor::IsEditorMenuCommandEnabled(EditorMenuCommand::Undo, withStacks(false, false)) &&
        GameEditor::IsEditorMenuCommandEnabled(EditorMenuCommand::Redo, withStacks(false, true));

    // 빌드 중에는 시작을 끄고 취소를 켜며, 대기 중에는 시작을 켜고 취소를 꺼야 한다.
    const auto whileBuilding = [](const bool isBuilding)
    {
        GameEditor::MenuCommandAvailability availability;
        availability.hasProject = true;
        availability.isBuilding = isBuilding;
        return availability;
    };
    const bool buildAndCancelAreOpposites =
        !GameEditor::IsEditorMenuCommandEnabled(EditorMenuCommand::Build, whileBuilding(true)) &&
        GameEditor::IsEditorMenuCommandEnabled(
            EditorMenuCommand::CancelBuild, whileBuilding(true)) &&
        GameEditor::IsEditorMenuCommandEnabled(EditorMenuCommand::Build, whileBuilding(false)) &&
        !GameEditor::IsEditorMenuCommandEnabled(
            EditorMenuCommand::CancelBuild, whileBuilding(false));

    // 그만둘 것이 없는데 프로젝트만 열려 있다고 켜지지는 않는다. 「돌고 있는가」 하나가
    // 이 항목을 정한다.
    const bool cancelNeedsARunningBuild =
        !enabled(EditorMenuCommand::CancelBuild, true, true, true);

    // 그리고 표에서 그만두기는 시작하기 <b>바로 아래</b>에 있다. 둘 중 하나만 보이는 것이
    // 아니라 둘 다 보이고 하나만 켜지므로, 떨어져 있으면 눈이 둘을 잇지 못한다.
    const std::span<const EditorMenuItem> fileRows = menus[0].items;
    bool cancelFollowsBuild = false;
    for (std::size_t index = 1; index < fileRows.size(); ++index)
    {
        if (fileRows[index].command == EditorMenuCommand::CancelBuild &&
            fileRows[index - 1].command == EditorMenuCommand::Build)
        {
            cancelFollowsBuild = true;
        }
    }

    // Exit는 File의 마지막이고, 선 하나로 그 앞과 갈라져 있다. 그리고 말줄임이 없다 —
    // 저장되지 않은 편집이 있을 때 묻는 것은 닫기 경로이지 이 항목이 아니며, 깨끗한
    // 편집기에서는 아무것도 묻지 않고 닫힌다.
    const std::span<const EditorMenuItem> fileItems = menus[0].items;
    const bool exitIsLastAndQuiet = !fileItems.empty() &&
        fileItems.back().label == "Exit" && fileItems.back().command == EditorMenuCommand::Exit &&
        !fileItems.back().asks && fileItems.back().shortcut.empty() &&
        fileItems.size() >= 2 && fileItems[fileItems.size() - 2].IsSeparator();

    // 닫는 것은 언제나 할 수 있다. 회색으로 만들면 나갈 수 없는 편집기가 된다.
    const bool exitAlwaysAvailable =
        GameEditor::IsEditorMenuCommandEnabled(EditorMenuCommand::Exit, {});

    return Expect(headingsAreThree, "the menu bar should carry File, Edit and Assets in order") &&
        Expect(exitIsLastAndQuiet, "Exit should end the File menu behind a separator, asking nothing") &&
        Expect(exitAlwaysAvailable, "Exit should never be greyed out — there must be a way out") &&
        Expect(everyShortcutWorks, "a menu should not promise a shortcut that nothing handles") &&
        Expect(separatorsSeparate, "separators should sit between items, never at either end") &&
        Expect(labelsAreUnique, "no two menu items should share a label") &&
        Expect(asksMarked, "only the items that ask again should be marked as asking") &&
        Expect(
            emptyEditorOffersOnlyProjects,
            "with nothing open only the two project commands should be available") &&
        Expect(
            sceneCommandsFollowTheirOwnTarget,
            "saving should follow the open scene and renaming the selected one") &&
        Expect(undoFollowsTheStack, "undo and redo should follow what the stack holds") &&
        Expect(
            buildAndCancelAreOpposites,
            "starting and cancelling a build should never be available at the same time") &&
        Expect(cancelNeedsARunningBuild, "cancelling should need a build that is actually running") &&
        Expect(cancelFollowsBuild, "Cancel Build should sit directly under Build");
}

static const TestSupport::Registration gEditorMenuModelTests{
    "EditorDocument", "editor menu model tests should pass", RunEditorMenuModelTests };
