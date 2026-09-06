#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <memory>

#include "Document/UndoStack.h"
#include "../GameEditor/Source/Document/EditorContext.h"
#include "../GameEditor/Source/Document/EditorPlaySession.h"

#include "EditorPlaySessionTests.h"
#include "TestSupport.h"

using GameEditor::EditorContext;
using GameEditor::EditorPlaySession;
using TestSupport::Expect;
using TestSupport::TemporaryDirectory;

namespace
{

[[nodiscard]] bool WriteTextFile(const std::filesystem::path& path, const std::string& contents)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        return false;
    }
    stream << contents;
    return stream.good();
}

/// <summary>장면 하나를 가진 최소 프로젝트를 만든다. 경로는 .gameproject 파일이다.</summary>
[[nodiscard]] std::filesystem::path WriteTestProject(const std::filesystem::path& root)
{
    const std::filesystem::path projectFile = root / "PlayTest.gameproject";
    const bool wrote =
        WriteTextFile(projectFile,
            R"({"projectName": "PlayTest",)"
            R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
            R"( "initialSceneId": 0, "scenes": [)"
            R"( { "id": 0, "path": "Scenes/First.scene" } ]})") &&
        WriteTextFile(root / "Scenes" / "First.scene",
            R"({"sceneName": "First", "gameObjects": []})");
    return wrote ? projectFile : std::filesystem::path{};
}

/// <summary>편집 하나를 기록해 「저장할 것이 있음」을 세운다. 내용은 중요하지 않다.</summary>
class NoOpEditCommand final : public GameEditor::IEditCommand
{
public:
    [[nodiscard]] bool Apply() override { return true; }
    [[nodiscard]] bool Revert() override { return true; }
};

}

bool RunEditorPlaySessionTests()
{
    std::cout << "running editor play session tests\n";
    bool passed = true;

    // ---- 부품 자신의 규칙 ----
    {
        EditorPlaySession session;
        passed = Expect(!session.IsPlaying(), "a fresh session should not be playing") && passed;
        passed = Expect(
            session.GetSnapshot().empty(),
            "a session that never began should hold no scene") && passed;

        passed = Expect(session.Begin("the scene as it was"), "beginning should be accepted")
            && passed;
        passed = Expect(session.IsPlaying(), "beginning should mark the session playing") && passed;

        // 두 번째 진입은 거절된다. 받아들이면 첫 진입의 텍스트를 덮어써서, 이탈이 사람이
        // 편집한 장면이 아니라 스크립트가 만든 상태로 되돌리게 된다.
        passed = Expect(
            !session.Begin("something else"),
            "beginning twice should be refused") && passed;
        passed = Expect(
            session.GetSnapshot() == "the scene as it was",
            "a refused second beginning should not replace the scene held") && passed;

        const std::string handedBack = session.End();
        passed = Expect(
            handedBack == "the scene as it was",
            "ending should hand back exactly what beginning was given") && passed;
        passed = Expect(!session.IsPlaying(), "ending should clear the playing mark") && passed;
        passed = Expect(
            session.GetSnapshot().empty(),
            "ending should leave nothing behind for the next play to restore") && passed;
    }

    // 문서가 통째로 바뀌면 표시와 텍스트를 버리되, 입력 포획은 그대로다 — 그것은 사람이 게임
    // 뷰를 눌러 잡고 Esc로 놓는 상태라 문서의 수명과 다른 축에 있다.
    {
        EditorPlaySession session;
        session.SetInputCaptured(true);
        static_cast<void>(session.Begin("a scene that is about to be closed"));
        session.Clear();
        passed = Expect(!session.IsPlaying(), "clearing should stop the play mark") && passed;
        passed = Expect(
            session.GetSnapshot().empty(), "clearing should drop the scene held") && passed;
        passed = Expect(
            session.IsInputCaptured(),
            "clearing the document should not release the person's input capture") && passed;
    }

    // ---- 부품이 건드리지 않는 것 ----
    TemporaryDirectory temporaryDirectory("editor-play-session");
    const std::filesystem::path projectFile = WriteTestProject(temporaryDirectory.GetPath());
    if (!Expect(!projectFile.empty(), "the play test project should be written"))
    {
        return false;
    }

    EditorContext context;
    passed = Expect(context.OpenProject(projectFile), "the test project should open") && passed;
    passed = Expect(context.HasOpenScene(), "opening a project should open its initial scene")
        && passed;

    context.RecordEdit(std::make_unique<NoOpEditCommand>());
    passed = Expect(
        context.HasUnsavedChanges(), "a recorded edit should mark the scene unsaved") && passed;

    // 다녀오는 것은 저장이 아니다. 이탈이 되돌리는 장면은 진입할 때의 상태 — 저장되지 않은
    // 편집을 담은 그 상태 — 이므로, 파일과의 차이는 다녀와도 그대로다.
    passed = Expect(context.EnterPlayMode(), "play mode should start") && passed;
    passed = Expect(
        context.HasUnsavedChanges(), "entering play should not clear the unsaved mark") && passed;
    passed = Expect(context.ExitPlayMode(), "play mode should stop") && passed;
    passed = Expect(
        context.HasUnsavedChanges(), "leaving play should not clear the unsaved mark either")
        && passed;

    return passed;
}

static const TestSupport::Registration gEditorPlaySessionTests{
    "EditorDocument", "editor play session tests should pass", RunEditorPlaySessionTests };
