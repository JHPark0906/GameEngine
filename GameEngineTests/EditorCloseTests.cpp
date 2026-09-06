#include "EditorCloseTests.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

#include "Rules/EditorCloseDecision.h"
#include "Platform/ChoiceDialogLayout.h"
#include "Document/EditorCommands.h"
#include "Document/EditorContext.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using TestSupport::TemporaryDirectory;
    using TestSupport::WriteFile;

    /// <summary>장면 둘을 가진 최소 프로젝트를 만든다. 경로는 .gameproject 파일이다.</summary>
    [[nodiscard]] std::filesystem::path WriteTestProject(const std::filesystem::path& root)
    {
        const std::filesystem::path projectFile = root / "EditorCloseTest.gameproject";
        const bool wrote =
            WriteFile(projectFile,
                R"({"projectName": "EditorCloseTest",)"
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
}

bool RunEditorCloseDecisionTests()
{
    using GameEditor::CloseDecision;
    using GameEditor::DecideOnCloseRequest;

    std::cout << "running editor close decision tests\n";

    // 저장할 것이 없으면 묻지 않고 닫는다. 묻는 것 자체가 방해이고, 답이 하나뿐인 물음이다.
    const CloseDecision nothingToSave = DecideOnCloseRequest(false, false);
    bool passed = Expect(
        nothingToSave.mayCloseNow, "with nothing to save the editor should just close");
    passed = Expect(!nothingToSave.shouldAsk, "with nothing to save there is nothing to ask") &&
        passed;

    // 저장할 것이 있으면 묻고, 그 답이 올 때까지 닫지 않는다. 여기서 곧바로 닫으면 사람이
    // 답할 기회 없이 편집을 잃는다.
    const CloseDecision unsaved = DecideOnCloseRequest(true, false);
    passed = Expect(unsaved.shouldAsk, "unsaved changes should raise the question") && passed;
    passed = Expect(
        !unsaved.mayCloseNow, "the editor should not close while the question is unanswered") &&
        passed;

    // 이미 묻고 있으면 다시 묻지 않는다. 닫기를 연타하면 같은 물음이 쌓이고, 사람은 답한
    // 만큼 다시 답해야 한다.
    const CloseDecision asking = DecideOnCloseRequest(true, true);
    passed = Expect(!asking.shouldAsk, "a second close request should not stack another question")
        && passed;
    passed = Expect(!asking.mayCloseNow, "the editor should still not close while asking") &&
        passed;

    // 묻는 도중에 저장이 끝났다면 — 다른 길로 저장이 일어날 수 있다 — 그다음 닫기 요청은
    // 곧바로 닫는다. 물어볼 것이 남아 있지 않다.
    const CloseDecision savedWhileAsking = DecideOnCloseRequest(false, true);
    passed = Expect(
        savedWhileAsking.mayCloseNow,
        "once there is nothing left to save the editor should close even if it was asking") &&
        passed;

    return passed;
}

namespace
{

    [[nodiscard]] std::filesystem::file_time_type WriteTimeOf(const std::filesystem::path& path)
    {
        std::error_code error;
        const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, error);
        return error ? std::filesystem::file_time_type{} : time;
    }

    /// <summary>
    /// 한 자리에서 저장을 재고 넷을 함께 본다: 반환값, 디스크의 바이트, mtime, 미저장 표시.
    /// 자리를 인자로 받는 것은 저장이 자리를 타는지 — 볼륨이나 임시 트리가 조건인지 — 를
    /// 두 자리에서 나란히 재기 위해서다.
    /// </summary>
    [[nodiscard]] bool MeasureSaveAt(const std::filesystem::path& root, const char* placeLabel)
    {
        const std::filesystem::path projectFile = WriteTestProject(root);
        if (projectFile.empty())
        {
            return Expect(false, "the save test project should be written");
        }
        const std::filesystem::path scenePath = root / "Scenes" / "First.scene";

        GameEditor::EditorContext context;
        if (!Expect(context.OpenProject(projectFile), "the save test project should open") ||
            !Expect(context.HasOpenScene(), "opening the project should open its scene"))
        {
            return false;
        }

        const std::string before = TestSupport::ReadFile(scenePath);
        // 파일 시각을 한 시간 뒤로 물려 둔다. 방금 쓴 파일과 곧 쓸 파일은 같은 시계 눈금
        // (윈도우에서 약 15ms)에 들어갈 수 있어, 물려 두지 않으면 "썼는데 시각이 같다"가
        // 나온다. 물려 두면 어떤 쓰기든 시각을 앞으로 옮기므로 비교가 결정적이다.
        std::error_code backdateError;
        const std::filesystem::file_time_type timeBefore =
            WriteTimeOf(scenePath) - std::chrono::hours(1);
        std::filesystem::last_write_time(scenePath, timeBefore, backdateError);
        bool prepared = Expect(!backdateError, "the scene file time should be movable");

        // 사람이 계층에서 Add를 누른 것과 같은 편집이다. 커맨드가 만들고, 기록이 미저장 표시를 세운다.
        auto command = std::make_unique<GameEditor::CreateGameObjectCommand>(context, "SaveProbe");
        bool passed = Expect(command->Apply(), "creating an object should succeed with a scene open") &&
            prepared;
        context.RecordEdit(std::move(command));
        passed =
            Expect(context.HasUnsavedChanges(), "a recorded edit should mark the scene unsaved") &&
            passed;

        // 여기가 이 시험의 전부다: 참을 돌려주면서 파일을 안 쓰는 경우가 있는가.
        const bool saveSaid = context.SaveOpenScene();
        const std::string after = TestSupport::ReadFile(scenePath);
        const std::filesystem::file_time_type timeAfter = WriteTimeOf(scenePath);
        const bool objectIsInTheFile = after.find("SaveProbe") != std::string::npos;

        std::cout << "  [" << placeLabel << "] save returned " << (saveSaid ? "true" : "false")
                  << ", bytes " << before.size() << " -> " << after.size()
                  << ", mtime " << (timeAfter != timeBefore ? "changed" : "unchanged")
                  << ", object in file: " << (objectIsInTheFile ? "yes" : "no")
                  << ", still unsaved: " << (context.HasUnsavedChanges() ? "yes" : "no") << "\n";

        passed = Expect(saveSaid, "saving an open scene should succeed") && passed;
        // 참을 돌려주었으면 파일에 그 편집이 있어야 한다. 이 둘이 어긋나면 사람은 저장한 줄 알고
        // 잃는다 — 저장이 거짓을 답하는 것보다 나쁘다.
        passed = Expect(
            objectIsInTheFile,
            "a save that reports success should leave the edit in the file on disk") && passed;
        passed =
            Expect(timeAfter != timeBefore, "a successful save should touch the file") && passed;
        passed = Expect(
            !context.HasUnsavedChanges(), "a successful save should clear the unsaved mark") &&
            passed;
        return passed;
    }
}

bool RunSaveWritesTheFileTests()
{
    std::cout << "running save-writes-the-file tests\n";

    TemporaryDirectory temporaryDirectory("save-writes");
    bool passed = MeasureSaveAt(temporaryDirectory.GetPath(), "temp volume");

    // 같은 조작을 임시 디렉터리 밖에서도 한 번 한다. 두 자리가 갈리면 자리가 조건이고,
    // 같이 통과하면 저장은 자리를 타지 않는다.
    std::error_code error;
    const std::filesystem::path working = std::filesystem::current_path(error);
    if (error)
    {
        return Expect(false, "the working directory should be readable") && passed;
    }
    // 이름에 프로세스 id가 들어간 임시 디렉터리와 같은 이름을 쓴다. 두 구성이 한꺼번에
    // 돌 때 같은 자리를 두고 부딪히지 않게 하는 것이 그 id의 일이다.
    const std::filesystem::path beside = working / temporaryDirectory.GetPath().filename();
    std::filesystem::remove_all(beside, error);
    passed = MeasureSaveAt(beside, "working volume") && passed;
    std::filesystem::remove_all(beside, error);
    return passed;
}

bool RunSaveFailureIsReportedTests()
{
    std::cout << "running save-failure-is-reported tests\n";

    TemporaryDirectory temporaryDirectory("save-failure");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::filesystem::path projectFile = WriteTestProject(root);
    if (projectFile.empty())
    {
        return Expect(false, "the save test project should be written");
    }
    const std::filesystem::path scenePath = root / "Scenes" / "First.scene";

    GameEditor::EditorContext context;
    if (!Expect(context.OpenProject(projectFile), "the save test project should open") ||
        !Expect(context.HasOpenScene(), "opening the project should open its scene"))
    {
        return false;
    }

    auto command = std::make_unique<GameEditor::CreateGameObjectCommand>(context, "FailProbe");
    bool passed = Expect(command->Apply(), "creating an object should succeed with a scene open");
    context.RecordEdit(std::move(command));

    // 쓰기를 실패시킨다. 대상을 읽기 전용으로 두면 원자적 쓰기의 마지막 걸음 — 임시 파일을
    // 제자리로 옮기는 것 — 이 거부당한다.
    std::error_code error;
    std::filesystem::permissions(
        scenePath, std::filesystem::perms::owner_read, std::filesystem::perm_options::replace,
        error);
    passed = Expect(!error, "the scene file should be made read-only for this test") && passed;

    const std::string before = TestSupport::ReadFile(scenePath);
    const bool saveSaid = context.SaveOpenScene();
    const std::string after = TestSupport::ReadFile(scenePath);
    const bool temporaryFileRemains =
        std::filesystem::exists(std::filesystem::path(scenePath) += ".tmp");

    std::filesystem::permissions(
        scenePath, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace,
        error);

    std::cout << "  save returned " << (saveSaid ? "true" : "false")
              << ", bytes " << before.size() << " -> " << after.size()
              << ", still unsaved: " << (context.HasUnsavedChanges() ? "yes" : "no")
              << ", temporary file left: " << (temporaryFileRemains ? "yes" : "no") << "\n";

    // 실패는 실패로 돌아와야 한다. 닫기 질문의 Save 갈래가 이 답 하나로 닫을지를 정하므로,
    // 실패가 참으로 돌아오면 사람은 저장한 줄 알고 창과 편집을 함께 잃는다.
    passed = Expect(!saveSaid, "a save that cannot write should report failure") && passed;
    passed = Expect(after == before, "a failed save should leave the file as it was") && passed;
    // 편집은 살아 있어야 한다. 미저장 표시가 내려가면 다음 물음이 오지 않는다.
    passed =
        Expect(context.HasUnsavedChanges(), "a failed save should keep the unsaved mark") && passed;
    passed = Expect(
        !temporaryFileRemains, "a failed save should not leave its temporary file behind") && passed;
    return passed;
}

bool RunCloseDialogLayoutTests()
{
    std::cout << "running close dialog layout tests\n";

    // 종료 물음이 실제로 세우는 셋이다. 가운데 것이 사본이 남았는지에 따라 갈린다.
    const std::vector<std::string> kept = {
        "Save and close", "Close and keep a copy", "Keep editing"
    };
    const std::vector<std::string> lost = {
        "Save and close", "Close and lose them", "Keep editing"
    };

    const auto check = [](const std::vector<std::string>& labels, const char* const what)
    {
        const GameEngine::Platform::ChoiceDialogLayout layout =
            GameEngine::Platform::ComputeChoiceDialogLayout(labels, 3);
        bool passed = Expect(
            layout.buttons.size() == labels.size(), "every button should get a place");
        if (!passed)
        {
            return false;
        }

        std::cout << "  [" << what << "] dialog " << layout.width << " wide, buttons";
        for (const GameEngine::Platform::ChoiceDialogRectangle& button : layout.buttons)
        {
            std::cout << " [" << button.left << "," << button.GetRight() << "]";
        }
        std::cout << "\n";

        for (std::size_t index = 0; index < layout.buttons.size(); ++index)
        {
            const GameEngine::Platform::ChoiceDialogRectangle& button = layout.buttons[index];
            // 버튼은 대화상자 안에 있어야 한다. 밖으로 나간 버튼은 그려지지 않는다.
            passed = Expect(button.left >= 0, "a button should not start before the dialog") &&
                passed;
            passed = Expect(
                button.GetRight() <= layout.width, "a button should end inside the dialog") &&
                passed;
            // 글이 들어갈 만큼은 넓어야 한다. 좁으면 잘려서 무엇을 누르는지 알 수 없다.
            passed = Expect(
                button.width >= static_cast<short>(labels[index].size() * 4),
                "a button should be wide enough for its label") && passed;
            if (index > 0)
            {
                passed = Expect(
                    button.left >= layout.buttons[index - 1].GetRight(),
                    "buttons should not overlap") && passed;
            }
        }
        // 설명 글도 버튼 줄과 겹치지 않아야 한다.
        passed = Expect(
            layout.message.top + layout.message.height <= layout.buttons.front().top,
            "the message should end above the buttons") && passed;
        passed = Expect(
            layout.buttons.front().top + layout.buttons.front().height <= layout.height,
            "the buttons should end inside the dialog") && passed;
        return passed;
    };

    bool passed = check(kept, "copy kept");
    passed = check(lost, "copy lost") && passed;

    // 긴 글은 잘리는 대신 버튼을 넓힌다 — 그것이 고정 폭과 다른 점이고, 이 시험의 이유다.
    const GameEngine::Platform::ChoiceDialogLayout narrow =
        GameEngine::Platform::ComputeChoiceDialogLayout({ "OK" }, 1);
    const GameEngine::Platform::ChoiceDialogLayout wide =
        GameEngine::Platform::ComputeChoiceDialogLayout(
            { "Close without saving and keep a copy for next time" }, 1);
    passed = Expect(
        wide.buttons.front().width > narrow.buttons.front().width,
        "a longer label should widen its button") && passed;
    passed = Expect(
        wide.width >= wide.buttons.front().GetRight(),
        "the dialog should widen with the button it holds") && passed;

    return passed;
}

bool RunCloseQuestionAndCopyTests()
{
    using GameEditor::MakeCloseQuestion;
    using GameEditor::ShouldKeepTheRecoveryCopy;
    using GameEditor::UnsavedChoice;

    std::cout << "running close question and copy tests\n";

    // 글은 사본이 실제로 남았는지에 따라 갈려야 한다. 갈리지 않으면 둘 중 한쪽이 거짓말이다.
    const std::string kept = MakeCloseQuestion(true);
    const std::string lost = MakeCloseQuestion(false);
    bool passed = Expect(kept != lost, "the question should say which of the two happened");
    passed = Expect(
        kept.find("has been kept") != std::string::npos,
        "when a copy was kept the question should say so as a fact") && passed;
    passed = Expect(
        kept.find("restore") != std::string::npos,
        "when a copy was kept the question should say it will be offered back") && passed;
    // 사본이 없을 때 "남긴다"고 말하면 사람은 그 말을 믿고 잃는다.
    passed = Expect(
        lost.find("has been kept") == std::string::npos,
        "when no copy was kept the question should not claim one was") && passed;
    passed = Expect(
        lost.find("loses the") != std::string::npos,
        "when no copy was kept the question should say the changes are lost") && passed;

    // 사본은 그것이 유일한 되돌릴 길일 때만 남는다.
    passed = Expect(
        ShouldKeepTheRecoveryCopy(UnsavedChoice::Discard, true),
        "closing without saving should keep the copy") && passed;
    passed = Expect(
        !ShouldKeepTheRecoveryCopy(UnsavedChoice::Cancel, false),
        "staying in the editor should not leave a copy behind to ask about") && passed;
    passed = Expect(
        !ShouldKeepTheRecoveryCopy(UnsavedChoice::Save, true),
        "a successful save puts the work in the file, so the copy should go") && passed;
    // 저장이 실패해 창이 남았으면 쓰지 못하는 무언가가 있다는 뜻이다. 그물을 걷지 않는다.
    passed = Expect(
        ShouldKeepTheRecoveryCopy(UnsavedChoice::Save, false),
        "a failed save should leave the copy in place") && passed;

    return passed;
}

static const TestSupport::Registration gEditorCloseDecisionTests{
    "EditorDocument", "editor close decision tests should pass", RunEditorCloseDecisionTests };

static const TestSupport::Registration gCloseDialogLayoutTests{
    "EditorDocument", "close dialog layout tests should pass", RunCloseDialogLayoutTests };

static const TestSupport::Registration gCloseQuestionAndCopyTests{
    "EditorDocument", "close question and copy tests should pass", RunCloseQuestionAndCopyTests };

static const TestSupport::Registration gSaveWritesTheFileTests{
    "EditorDocument", "save writes the file tests should pass", RunSaveWritesTheFileTests };

static const TestSupport::Registration gSaveFailureIsReportedTests{
    "EditorDocument", "save failure is reported tests should pass", RunSaveFailureIsReportedTests };
