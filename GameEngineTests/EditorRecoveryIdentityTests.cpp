#include "EditorRecoveryIdentityTests.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

#include "Document/EditorContext.h"
#include "Rules/EditorRecovery.h"
#include "TestSupport.h"

bool RunEditorRecoveryIdentityTests()
{
    namespace fs = std::filesystem;
    using TestSupport::Expect;
    using TestSupport::ReadFile;
    using TestSupport::WriteFile;
    TestSupport::TemporaryDirectory temporary("editor-recovery-identity");
    const fs::path root = temporary.GetPath();
    const fs::path first = root / "First" / "Clone.gameproject";
    const fs::path second = root / "Second" / "Clone.gameproject";
    const fs::path recovery = root / "Recovery";
    const std::string settings = R"({"projectName":"Clone","initialSceneId":0,"window":{"width":1280,"height":720},)"
        R"("scenes":[{"id":0,"path":"Main.scene"}]})";
    const std::string original = R"({"sceneName":"Original","gameObjects":[]})";
    const bool prepared = WriteFile(first, settings) && WriteFile(second, settings) &&
        WriteFile(first.parent_path() / "Main.scene", original) &&
        WriteFile(second.parent_path() / "Main.scene", original);
    if (!Expect(prepared, "same-named projects should be written in separate directories")) return false;

    const std::string firstText = R"({"sceneName":"FirstRecovery","gameObjects":[]})";
    const std::string secondText = R"({"sceneName":"SecondRecovery","gameObjects":[]})";
    const fs::path firstCopy = GameEditor::WriteRecoveryFile(first, 0, firstText, recovery);
    const fs::path secondCopy = GameEditor::WriteRecoveryFile(second, 0, secondText, recovery);
    bool passed = Expect(!firstCopy.empty() && !secondCopy.empty() && firstCopy != secondCopy,
        "same-stem clones must reserve different recovery files");
    passed = Expect(ReadFile(firstCopy) == firstText && ReadFile(secondCopy) == secondText,
        "saving a clone's recovery must preserve the other clone's unsaved work") && passed;

    std::wstring alternateCase = first.wstring();
    std::ranges::transform(alternateCase, alternateCase.begin(), [](const wchar_t c)
    {
        return static_cast<wchar_t>(std::towupper(c));
    });
    const fs::path alternateCopy =
        GameEditor::MakeRecoveryPath(fs::path(alternateCase), 0, recovery);
    std::error_code caseError;
    passed = Expect(alternateCopy.parent_path() == firstCopy.parent_path() &&
        fs::equivalent(alternateCopy, firstCopy, caseError) && !caseError,
        "Windows path casing must not split one project's recovery directory") && passed;
    const auto all = GameEditor::CollectRecoverySnapshots(recovery);
    const auto mine = GameEditor::FindSnapshotsForProject(all, first);
    passed = Expect(all.size() == 2 && mine.size() == 1 && mine.front().filePath == firstCopy,
        "snapshot discovery should select the full project identity") && passed;

    GameEditor::EditorContext context;
    if (!Expect(context.OpenProject(second), "the second clone should open")) return false;
    passed = Expect(!context.RestoreRecoverySnapshot(firstCopy, 0),
        "restoration must reject another clone's recovery even with the same scene id") && passed;
    passed = Expect(context.RestoreRecoverySnapshot(secondCopy, 0),
        "restoration should accept the current project's recovery") && passed;
    passed = Expect(ReadFile(firstCopy) == firstText,
        "a rejected restoration must leave the other clone's snapshot intact") && passed;

    // Unowned legacy files remain available for manual recovery, but cannot prove a project match.
    const fs::path legacy = recovery / "Clone.scene0.recovered.json";
    passed = Expect(WriteFile(legacy, "legacy unsaved work"), "a legacy copy should be written") && passed;
    const auto withLegacy = GameEditor::CollectRecoverySnapshots(recovery);
    passed = Expect(withLegacy.size() == 3 &&
        GameEditor::FindSnapshotsForProject(withLegacy, first).size() == 1 &&
        ReadFile(legacy) == "legacy unsaved work",
        "legacy copies must be preserved without attributing them to a same-named project") && passed;

    // A stale or colliding directory owner cannot silently receive another project's snapshot.
    const std::u8string wrongOwner = second.generic_u8string();
    passed = Expect(WriteFile(firstCopy.parent_path() / "project.path",
        std::string(wrongOwner.begin(), wrongOwner.end())), "a mismatched owner should be written") && passed;
    passed = Expect(GameEditor::WriteRecoveryFile(first, 0, "replacement", recovery).empty() &&
        ReadFile(firstCopy) == firstText,
        "an ownership mismatch must fail without replacing existing recovery data") && passed;
    return passed;
}

static const TestSupport::Registration gEditorRecoveryIdentityTests{
    "EditorDocument", "recovery snapshots isolate same-named project checkouts",
    RunEditorRecoveryIdentityTests };
