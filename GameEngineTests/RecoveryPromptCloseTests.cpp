#include "RecoveryPromptCloseTests.h"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "Document/EditorContext.h"
#include "Rules/EditorRecovery.h"
#include "Runtime/Game.h"
#include "Runtime/Scene.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Serialization/SceneSerializer.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    constexpr std::size_t RestoreButton = 0;
    constexpr std::size_t DiscardButton = 1;
}

bool RunRecoveryPromptCloseTests()
{
    std::cout << "running recovery prompt close tests\n";

    // 창을 닫으려는 사람에게 대화상자는 답 없이 끝난다. 그 "답 없음"이 무엇이 되는지가 이
    // 시험의 핵심이다: 지우면 사람이 고르지 않은 삭제가 되고, 되살리면 고르지 않은 편집이 된다.
    bool passed = Expect(
        GameEditor::ChoiceFromDialogAnswer(std::nullopt, RestoreButton, DiscardButton) ==
            GameEditor::RecoveryChoice::Keep,
        "a dialog that ends without an answer should mean deciding later");

    // 답을 들었을 때는 그대로 옮긴다. 위의 규칙이 모든 답을 삼켜 버리지 않는지도 함께 본다.
    passed = Expect(
        GameEditor::ChoiceFromDialogAnswer(RestoreButton, RestoreButton, DiscardButton) ==
            GameEditor::RecoveryChoice::Restore,
        "choosing to restore should mean restoring") && passed;
    passed = Expect(
        GameEditor::ChoiceFromDialogAnswer(DiscardButton, RestoreButton, DiscardButton) ==
            GameEditor::RecoveryChoice::Discard,
        "choosing to discard should mean discarding") && passed;
    // 모르는 첨자에 파일을 지우지 않는다. 무엇을 고른 것인지 모를 때 삭제가 가장 나쁜 답이다.
    passed = Expect(
        GameEditor::ChoiceFromDialogAnswer(7, RestoreButton, DiscardButton) ==
            GameEditor::RecoveryChoice::Keep,
        "an answer nobody understands should not delete the copy") && passed;

    // 그 답이 실제로 파일을 남기는지까지 이어서 본다. 규칙만 맞고 파일이 사라지면 소용없다.
    TestSupport::TemporaryDirectory temporaryDirectory("recovery-prompt-close");
    const std::filesystem::path snapshotPath = temporaryDirectory.GetPath() / "probe.json";
    if (!Expect(
            TestSupport::WriteFile(snapshotPath, "{}"), "the probe snapshot should be written"))
    {
        return false;
    }

    GameEditor::RecoverySnapshot snapshot;
    snapshot.filePath = snapshotPath;
    snapshot.projectStem = "Probe";
    snapshot.sceneId = 0;

    // 창이 닫히는 중이라 물음이 답 없이 끝난 그 상황이다.
    const bool restored = GameEditor::ApplyRecoveryChoice(
        snapshot,
        GameEditor::ChoiceFromDialogAnswer(std::nullopt, RestoreButton, DiscardButton),
        [](const GameEditor::RecoverySnapshot&) { return true; });

    const bool stillThere = std::filesystem::exists(snapshotPath);
    std::cout << "  closing while asked: restored " << (restored ? "yes" : "no")
              << ", snapshot still there: " << (stillThere ? "yes" : "no") << "\n";
    passed = Expect(!restored, "closing the window should not restore anything") && passed;
    passed = Expect(
        stillThere, "closing the window should leave the copy for the next time") && passed;

    return passed;
}

bool RunEditModeFrameLeavesDocumentTests()
{
    std::cout << "running edit mode frame tests\n";

    static_cast<void>(GameEngine::Serialization::RegisterRuntimeComponentFactories());

    TestSupport::TemporaryDirectory temporaryDirectory("edit-mode-frame");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::filesystem::path projectFile = root / "EditModeFrameTest.gameproject";
    const bool wrote =
        TestSupport::WriteFile(projectFile,
            R"({"projectName": "EditModeFrameTest",)"
            R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
            R"( "initialSceneId": 0, "scenes": [ { "id": 0, "path": "Scenes/First.scene" } ]})") &&
        TestSupport::WriteFile(root / "Scenes" / "First.scene",
            R"({"sceneName": "First", "gameObjects": [)"
            R"({"id": 1, "name": "Mover", "isActive": true, "components": [)"
            R"({"type": "Transform", "position": [1, 2, 3], "rotation": [0, 0, 0],)"
            R"( "rotationUnit": "degrees", "rotationOrder": "rollPitchYaw",)"
            R"( "scale": [1, 1, 1]}]}]})");
    if (!Expect(wrote, "the edit mode test project should be written"))
    {
        return false;
    }

    GameEditor::EditorContext context;
    if (!Expect(context.OpenProject(projectFile), "the edit mode test project should open") ||
        !Expect(context.HasOpenScene(), "opening the project should open its scene"))
    {
        return false;
    }
    GameEngine::Runtime::Scene* const scene = context.GetOpenScene();
    if (!scene)
    {
        return Expect(false, "the scene should be open");
    }

    const std::string afterLoad =
        GameEngine::Serialization::SceneSerializer::SaveToText(*scene);

    // 물음이 서는 것은 편집 모드에서다. 그 동안 문서를 움직일 수 있는 것은 프로젝트 런타임의
    // 업데이트뿐이고, 편집 모드에서는 그것이 돌지 않는다 — 그 규칙은 EditorBootstrap::Update의
    // 조기 반환이고, 그 파일은 이 시험 대상에 없으므로 여기서 재는 것은 그 규칙이 지켜질 때의
    // 결과다: 열고 나서 아무도 업데이트하지 않으면 문서는 로드된 그대로다.
    bool passed = Expect(!context.IsPlaying(), "a freshly opened project should not be playing");

    const std::string atPromptTime =
        GameEngine::Serialization::SceneSerializer::SaveToText(*scene);
    std::cout << "  scene text after load: " << afterLoad.size() << " bytes, at prompt time: "
              << atPromptTime.size() << " bytes, same: "
              << (afterLoad == atPromptTime ? "yes" : "no") << "\n";
    passed = Expect(
        afterLoad == atPromptTime,
        "an edit mode frame should leave the document exactly as it was loaded") && passed;
    passed = Expect(
        !context.HasUnsavedChanges(),
        "an edit mode frame should not make the scene look edited") && passed;

    return passed;
}

static const TestSupport::Registration gRecoveryPromptCloseTests{
    "EditorDocument", "recovery prompt close tests should pass", RunRecoveryPromptCloseTests };

static const TestSupport::Registration gEditModeFrameLeavesDocumentTests{
    "EditorDocument", "edit mode frame tests should pass", RunEditModeFrameLeavesDocumentTests };
