#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "../GameEditor/Source/Document/EditorContext.h"
#include "../GameEngine/Core/UndoStack.h"

#include "SceneDocumentCloseTests.h"
#include "TestSupport.h"

using GameEditor::EditorContext;
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

/// <summary>장면 둘을 가진 프로젝트다. 둘째는 파일을 쓰지 않으면 열리지 않는다.</summary>
[[nodiscard]] std::filesystem::path WriteProject(
    const std::filesystem::path& root, const std::string& name, const bool writeSceneFile)
{
    const std::filesystem::path projectFile = root / (name + ".gameproject");
    const bool wrote =
        WriteTextFile(projectFile,
            R"({"projectName": ")" + name + R"(",)"
            R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
            R"( "initialSceneId": 0, "scenes": [)"
            R"( { "id": 0, "path": "Scenes/First.scene" },)"
            R"( { "id": 1, "path": "Scenes/Second.scene" } ]})") &&
        (!writeSceneFile ||
            (WriteTextFile(root / "Scenes" / "First.scene",
                 R"({"sceneName": "First", "gameObjects": []})") &&
             WriteTextFile(root / "Scenes" / "Second.scene",
                 R"({"sceneName": "Second", "gameObjects": []})")));
    return wrote ? projectFile : std::filesystem::path{};
}

class NoOpEditCommand final : public GameEngine::Core::IEditCommand
{
public:
    [[nodiscard]] bool Apply() override { return true; }
    [[nodiscard]] bool Revert() override { return true; }
};

}

bool RunSceneDocumentCloseTests()
{
    std::cout << "running scene document close tests\n";

    TemporaryDirectory temporaryDirectory("scene-document-close");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::filesystem::path first = WriteProject(root / "First", "First", true);
    // 두 번째 프로젝트는 장면 파일이 없다. 프로젝트는 열리지만 장면은 열리지 않으므로, 이전
    // 문서를 지운 것이 닫기 하나뿐인 상태를 만들 수 있다 — 새 장면이 올라오면 그것이 다시
    // 지우기 때문에 닫기가 제 일을 했는지 알 수 없게 된다.
    const std::filesystem::path second = WriteProject(root / "Second", "Second", false);
    if (!Expect(!first.empty() && !second.empty(), "both test projects should be written"))
    {
        return false;
    }

    bool passed = true;

    // ---- 프로젝트를 열면 이전 문서의 흔적이 남지 않는다 ----
    {
        EditorContext context;
        passed = Expect(context.OpenProject(first), "the first project should open") && passed;
        passed = Expect(context.HasOpenScene(), "its initial scene should open") && passed;

        // 사람이 무언가 한 상태를 만든다: 편집, 오브젝트 선택, 장면 선택.
        //
        // 플레이는 여기 함께 둘 수 없다. 진입이 되돌리기 이력을 비우는 것이 규칙이라
        // — 되살린 장면의 인스턴스 id가 전부 새것이라 커맨드가 고아가 된다 — 이력과
        // 플레이가 함께 서 있는 상태는 만들어지지 않는다. 플레이는 아래에서 따로 잰다.
        context.RecordEdit(std::make_unique<NoOpEditCommand>());
        context.SelectObject(1234);
        context.SelectScene(1);
        const bool madeState = context.HasUnsavedChanges() &&
            context.GetSelectedInstanceId() == 1234 && context.GetUndoStack().CanUndo();
        if (!Expect(madeState, "the test should have made a document worth closing"))
        {
            return false;
        }

        passed = Expect(
            context.OpenProject(second),
            "the second project should open even though its scene file is missing") && passed;
        passed = Expect(
            !context.HasOpenScene(),
            "no scene should be open, so only the close cleared the old document") && passed;

        passed = Expect(
            !context.HasUnsavedChanges(),
            "closing should forget that the old document had unsaved edits") && passed;
        passed = Expect(
            context.GetSelectedInstanceId() == 0,
            "closing should forget which object was selected") && passed;
        passed = Expect(
            !context.GetUndoStack().CanUndo(),
            "closing should forget the old document's undo history") && passed;
        passed = Expect(
            !context.IsPlaying(),
            "a document that was never playing should still not be playing") && passed;
    }

    // 플레이 중에 프로젝트를 바꾸면 플레이도 함께 끝난다. 되돌아갈 장면이 사라졌기 때문이다.
    {
        EditorContext context;
        passed = Expect(context.OpenProject(first), "the first project should open") && passed;
        passed = Expect(context.EnterPlayMode(), "play mode should start") && passed;
        passed = Expect(
            context.OpenProject(second), "the second project should open") && passed;
        passed = Expect(
            !context.IsPlaying(),
            "closing should leave play mode, since the scene it was playing is gone") && passed;
    }

    // ---- 장면이 열리지 않으면 설정도 리비전도 움직이지 않는다 ----
    //
    // 「다음 부팅이 돌아올 장면」과 리비전은 올린 뒤에만 적힌다. 뒤집히면 다음 실행이 열리지
    // 않는 장면으로 돌아가고, 화면을 보는 것들이 바뀌지 않은 장면을 다시 읽는다.
    {
        EditorContext context;
        passed = Expect(context.OpenProject(first), "the first project should open") && passed;
        const unsigned int revisionBefore = context.GetProjectRevision();
        const unsigned int rememberedBefore = context.GetSettings().lastSceneId;
        const unsigned int openBefore = context.GetOpenProjectSceneId();

        // 프로젝트에 없는 장면 id.
        passed = Expect(
            !context.OpenScene(99), "a scene the project does not list should not open") && passed;
        passed = Expect(
            context.GetProjectRevision() == revisionBefore &&
                context.GetSettings().lastSceneId == rememberedBefore &&
                context.GetOpenProjectSceneId() == openBefore,
            "a scene that did not open should change neither the revision nor what is remembered")
            && passed;

        // 등록은 되어 있으나 파일이 읽히지 않는 장면. 위와 다른 실패 지점이지만 약속은 같다.
        std::error_code error;
        std::filesystem::remove(root / "First" / "Scenes" / "Second.scene", error);
        passed = Expect(!error, "the second scene file should be removable") && passed;
        passed = Expect(
            !context.OpenScene(1), "a scene whose file is gone should not open") && passed;
        passed = Expect(
            context.GetProjectRevision() == revisionBefore &&
                context.GetSettings().lastSceneId == rememberedBefore &&
                context.GetOpenProjectSceneId() == openBefore,
            "a scene that failed to load should leave the revision and the memory alone")
            && passed;

        // 열리는 장면으로는 둘 다 움직인다 — 위의 단언이 「아무것도 안 움직인다」를 재는 것이
        // 아니라 순서를 재는 것임을 여기서 못박는다.
        passed = Expect(context.OpenScene(0), "the first scene should open") && passed;
        passed = Expect(
            context.GetProjectRevision() != revisionBefore &&
                context.GetSettings().lastSceneId == 0,
            "a scene that opened should bump the revision and be remembered") && passed;
    }

    return passed;
}

static const TestSupport::Registration gSceneDocumentCloseTests{
    "EditorDocument", "scene document close tests should pass", RunSceneDocumentCloseTests };
