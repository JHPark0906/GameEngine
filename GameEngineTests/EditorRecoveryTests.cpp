#include "EditorRecoveryTests.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Document/EditorCommands.h"
#include "Document/EditorContext.h"
#include "Rules/EditorRecovery.h"
#include "Runtime/GameObject.h"
#include "Runtime/Scene.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using TestSupport::TemporaryDirectory;
    using TestSupport::WriteFile;

    /// <summary>장면 둘을 가진 최소 프로젝트를 만든다. 경로는 .gameproject 파일이다.</summary>
    [[nodiscard]] std::filesystem::path WriteTestProject(const std::filesystem::path& root)
    {
        const std::filesystem::path projectFile = root / "EditorRecoveryTest.gameproject";
        const bool wrote =
            WriteFile(projectFile,
                R"({"projectName": "EditorRecoveryTest",)"
                R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
                R"( "initialSceneId": 0, "scenes": [)"
                R"( { "id": 0, "path": "Scenes/First.scene" },)"
                R"( { "id": 1, "path": "Scenes/Second.scene" } ]})") &&
            WriteFile(root / "Scenes" / "First.scene",
                R"({"sceneName": "First", "gameObjects": []})") &&
            WriteFile(root / "Scenes" / "Second.scene",
                R"({"sceneName": "Second", "gameObjects": []})");
        return wrote ? projectFile : std::filesystem::path{};
    }

    [[nodiscard]] bool SceneHasObject(
        const GameEditor::EditorContext& context, const std::string& name)
    {
        const GameEngine::Runtime::Scene* const scene = context.GetOpenScene();
        if (!scene)
        {
            return false;
        }
        const std::vector<const GameEngine::Runtime::GameObject*> objects =
            scene->GetRootGameObjects();
        return std::any_of(
            objects.begin(), objects.end(),
            [&name](const GameEngine::Runtime::GameObject* const object)
            { return object && object->GetName() == name; });
    }
}

bool RunEditorRecoveryRuleTests()
{
    std::cout << "running editor recovery rule tests\n";

    // 쓰는 쪽과 읽는 쪽이 같은 규칙을 보는지가 이 시험의 첫째다. 규칙이 두 벌이면 "쓴 파일을
    // 못 찾는다"가 아무 소리도 내지 않는 실패가 된다.
    const std::filesystem::path madePath =
        GameEditor::MakeRecoveryPath(
            std::filesystem::path("C:/games/Wanderer.gameproject"), 3, "C:/probe");
    bool passed = Expect(!madePath.empty(), "a recovery path should be made");

    std::string stem;
    unsigned int sceneId = 0;
    passed = Expect(
        GameEditor::ParseRecoveryFileName(madePath, stem, sceneId),
        "the name the rule makes should be a name the rule can read back") && passed;
    passed = Expect(stem == "Wanderer", "the project name should survive the round trip") && passed;
    passed = Expect(sceneId == 3, "the scene id should survive the round trip") && passed;

    // 프로젝트 이름에 장면 표시가 들어가도 마지막 것에서 갈라야 한다.
    const std::filesystem::path awkward =
        GameEditor::MakeRecoveryPath(
            std::filesystem::path("C:/games/My.scene.gameproject"), 12, "C:/probe");
    passed = Expect(
        GameEditor::ParseRecoveryFileName(awkward, stem, sceneId) && stem == "My.scene" &&
            sceneId == 12,
        "a project name containing the scene marker should still round trip") && passed;

    // 남의 파일은 건드리지 않는다. 형식이 아니면 복구 파일이 아니다.
    passed = Expect(
        !GameEditor::ParseRecoveryFileName(std::filesystem::path("notes.txt"), stem, sceneId),
        "an unrelated file should not read as a recovery snapshot") && passed;
    passed = Expect(
        !GameEditor::ParseRecoveryFileName(
            std::filesystem::path("Wanderer.sceneX.recovered.json"), stem, sceneId),
        "a snapshot name without a scene number should be rejected") && passed;
    passed = Expect(
        !GameEditor::ParseRecoveryFileName(
            std::filesystem::path(".scene3.recovered.json"), stem, sceneId),
        "a snapshot name without a project name should be rejected") && passed;

    // 주인이 사라진 사본을 골라내는 것이 목록의 둘째 일이다. 남아 쌓이기만 하는 파일이라
    // 사람이 날짜를 보고 지울 수 있어야 한다.
    TemporaryDirectory owners("editor-recovery-rule-owners");
    const std::filesystem::path livingProject = owners.GetPath() / "Living.gameproject";
    passed = Expect(WriteFile(livingProject, "{}"), "the living project should exist") && passed;
    std::vector<GameEditor::RecoverySnapshot> snapshots;
    snapshots.push_back({ "a.scene0.recovered.json", "Living", 0, {}, livingProject });
    snapshots.push_back({ "b.scene1.recovered.json", "Gone", 1, {} });
    const std::vector<GameEditor::RecoverySnapshot> mine = GameEditor::FindSnapshotsForProject(
        snapshots, livingProject);
    passed = Expect(
        mine.size() == 1 && mine.front().projectStem == "Living",
        "a project should find only its own snapshots") && passed;

    const std::vector<GameEditor::RecoverySnapshot> orphans = GameEditor::FindOrphanSnapshots(
        snapshots, [](const std::string& projectStem) { return projectStem == "Living"; });
    passed = Expect(
        orphans.size() == 1 && orphans.front().projectStem == "Gone",
        "a snapshot whose project is gone should be listed as an orphan") && passed;

    return passed;
}

bool RunEditorRecoveryRestoreTests()
{
    std::cout << "running editor recovery restore tests\n";

    TemporaryDirectory temporaryDirectory("editor-recovery");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::filesystem::path projectFile = WriteTestProject(root);
    if (projectFile.empty())
    {
        return Expect(false, "the recovery test project should be written");
    }

    GameEditor::EditorContext context;
    if (!Expect(context.OpenProject(projectFile), "the recovery test project should open") ||
        !Expect(context.HasOpenScene(), "opening the project should open its scene"))
    {
        return false;
    }

    // 사람이 편집하고 저장하지 않은 상태를 만든 다음, 사고가 남기는 것과 같은 사본을 뜬다.
    auto command = std::make_unique<GameEditor::CreateGameObjectCommand>(context, "LostWork");
    bool passed = Expect(command->Apply(), "creating an object should succeed with a scene open");
    context.RecordEdit(std::move(command));

    // 사본은 이 프로세스만의 자리에 둔다. 실제 복구 디렉터리에 고정된 이름으로 쓰면, 같은
    // 순간에 도는 다른 시험 프로세스와 같은 파일을 두고 다투게 되고 지우기가 실패한다.
    const std::filesystem::path recoveryDirectory = root / "Recovery";
    const std::filesystem::path snapshotPath = context.SaveRecoverySnapshot(recoveryDirectory);
    passed = Expect(!snapshotPath.empty(), "a recovery snapshot should be written") && passed;
    passed = Expect(
        snapshotPath ==
            GameEditor::MakeRecoveryPath(
                projectFile, context.GetOpenProjectSceneId(), recoveryDirectory),
        "the snapshot should land where the shared rule says it does") && passed;

    // 목록이 방금 쓴 것을 실제로 찾아내는지 — 규칙 하나를 두 쪽이 함께 보는지가 여기서 갈린다.
    const std::vector<GameEditor::RecoverySnapshot> found =
        GameEditor::FindSnapshotsForProject(
            GameEditor::CollectRecoverySnapshots(recoveryDirectory), projectFile);
    passed = Expect(
        found.size() == 1 && found.front().filePath == snapshotPath,
        "collecting should find the snapshot that was just written") && passed;

    // 사고를 흉내낸다: 저장하지 않은 편집을 잃은 채로 파일에서 다시 연다.
    passed = Expect(context.OpenScene(0), "reopening the scene should succeed") && passed;
    passed = Expect(
        !SceneHasObject(context, "LostWork"),
        "reopening from disk should not have the unsaved edit") && passed;
    passed = Expect(
        !context.HasUnsavedChanges(), "a scene read from disk should have nothing to save") &&
        passed;

    const bool restored = context.RestoreRecoverySnapshot(snapshotPath, 0);
    std::cout << "  restore returned " << (restored ? "true" : "false")
              << ", object back: " << (SceneHasObject(context, "LostWork") ? "yes" : "no")
              << ", unsaved: " << (context.HasUnsavedChanges() ? "yes" : "no")
              << ", snapshot still there: "
              << (std::filesystem::exists(snapshotPath) ? "yes" : "no") << "\n";

    passed = Expect(restored, "restoring a snapshot should succeed") && passed;
    passed = Expect(
        SceneHasObject(context, "LostWork"), "restoring should bring the lost edit back") && passed;
    // 되살린 장면은 저장되지 않은 상태로 서야 한다. 저장할 것이 없다고 말하면 사람은 되살린
    // 작업을 두 번째로 잃는다.
    passed = Expect(
        context.HasUnsavedChanges(), "a restored scene should be marked unsaved") && passed;
    // 되살리기는 사본을 지우지 않는다. 지우는 것은 사람이 버린다고 답했을 때만이다.
    passed = Expect(
        std::filesystem::exists(snapshotPath),
        "restoring should leave the snapshot for a second attempt") && passed;

    passed =
        Expect(
            GameEditor::DeleteRecoverySnapshot(snapshotPath),
            "discarding should delete the snapshot") && passed;
    passed = Expect(
        !std::filesystem::exists(snapshotPath), "a discarded snapshot should be gone") && passed;
    // 이미 없는 것을 지우는 것은 실패가 아니다 — 사람이 손으로 지웠을 수 있고, 그때 오류를
    // 내면 다음 열기가 없는 파일을 두고 묻는다.
    passed = Expect(
        GameEditor::DeleteRecoverySnapshot(snapshotPath),
        "deleting an already-missing snapshot should not be a failure") && passed;

    return passed;
}

bool RunEditorRecoveryChoiceTests()
{
    std::cout << "running editor recovery choice tests\n";

    TemporaryDirectory temporaryDirectory("editor-recovery-choice");
    const std::filesystem::path root = temporaryDirectory.GetPath();

    const auto makeSnapshot = [&root](const char* const name)
    {
        GameEditor::RecoverySnapshot snapshot;
        snapshot.filePath = root / name;
        snapshot.projectStem = "Probe";
        snapshot.sceneId = 0;
        static_cast<void>(WriteFile(snapshot.filePath, "{}"));
        return snapshot;
    };
    const auto alwaysSucceeds = [](const GameEditor::RecoverySnapshot&) { return true; };
    const auto alwaysFails = [](const GameEditor::RecoverySnapshot&) { return false; };

    // 버리기는 파일을 지운다. 그것이 사람이 버린다고 답했다는 뜻이다.
    const GameEditor::RecoverySnapshot discarded = makeSnapshot("discard.json");
    bool passed = Expect(
        !GameEditor::ApplyRecoveryChoice(
            discarded, GameEditor::RecoveryChoice::Discard, alwaysSucceeds),
        "discarding should not report a restore");
    passed = Expect(
        !std::filesystem::exists(discarded.filePath), "discarding should delete the file") && passed;

    // 나중에 정하기는 아무것도 하지 않는다. 파일이 남아야 다음 열기에서 다시 물을 수 있다.
    const GameEditor::RecoverySnapshot kept = makeSnapshot("keep.json");
    passed = Expect(
        !GameEditor::ApplyRecoveryChoice(kept, GameEditor::RecoveryChoice::Keep, alwaysSucceeds),
        "keeping should not report a restore") && passed;
    passed =
        Expect(std::filesystem::exists(kept.filePath), "keeping should leave the file") && passed;

    // 되살리기는 파일을 지우지 않는다. 저장하기 전에 또 사고가 나면 그 파일이 다시 유일한
    // 되돌릴 길이다.
    const GameEditor::RecoverySnapshot restored = makeSnapshot("restore.json");
    passed = Expect(
        GameEditor::ApplyRecoveryChoice(
            restored, GameEditor::RecoveryChoice::Restore, alwaysSucceeds),
        "restoring should report a restore") && passed;
    passed = Expect(
        std::filesystem::exists(restored.filePath), "restoring should leave the file") && passed;

    // 되살리기가 실패해도 지우지 않는다. 실패한 뒤 파일까지 없으면 사람은 두 번 잃는다.
    const GameEditor::RecoverySnapshot failed = makeSnapshot("failed.json");
    passed = Expect(
        !GameEditor::ApplyRecoveryChoice(
            failed, GameEditor::RecoveryChoice::Restore, alwaysFails),
        "a failed restore should not report a restore") && passed;
    passed = Expect(
        std::filesystem::exists(failed.filePath),
        "a failed restore should leave the file for a second attempt") && passed;

    // 되살릴 길이 아예 없어도 지우지 않는다.
    const GameEditor::RecoverySnapshot unrestorable = makeSnapshot("unrestorable.json");
    passed = Expect(
        !GameEditor::ApplyRecoveryChoice(unrestorable, GameEditor::RecoveryChoice::Restore, {}),
        "with nobody to restore it, nothing should be restored") && passed;
    passed = Expect(
        std::filesystem::exists(unrestorable.filePath),
        "with nobody to restore it, nothing should be deleted") && passed;

    const std::string when = GameEditor::FormatSnapshotTime(
        std::filesystem::last_write_time(unrestorable.filePath));
    std::cout << "  a snapshot time reads as \"" << when << "\"\n";
    passed = Expect(
        when.size() == 16 && when[4] == '-' && when[7] == '-' && when[13] == ':',
        "a snapshot time should read as a date and a clock time") && passed;

    // 방금 쓴 파일이므로 그 글은 지금의 지역 시각이어야 한다. UTC로 적으면 여기서 갈린다.
    const std::string nowLocal = std::format(
        "{:%Y-%m-%d %H:%M}",
        std::chrono::floor<std::chrono::minutes>(
            std::chrono::current_zone()->to_local(std::chrono::system_clock::now())));
    passed = Expect(
        when.substr(0, 13) == nowLocal.substr(0, 13),
        "a snapshot written just now should read as the local date and hour") && passed;

    return passed;
}

static const TestSupport::Registration gEditorRecoveryRuleTests{
    "EditorDocument", "editor recovery rule tests should pass", RunEditorRecoveryRuleTests };

static const TestSupport::Registration gEditorRecoveryRestoreTests{
    "EditorDocument", "editor recovery restore tests should pass", RunEditorRecoveryRestoreTests };

static const TestSupport::Registration gEditorRecoveryChoiceTests{
    "EditorDocument", "editor recovery choice tests should pass", RunEditorRecoveryChoiceTests };
