#include "ProjectContentRootTests.h"

#include <filesystem>
#include <iostream>
#include <string>

#include "App/ProjectFile.h"
#include "TestSupport.h"

using TestSupport::Expect;

using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{

    [[nodiscard]] std::string ProjectText(const std::string& name)
    {
        return R"({"projectName": ")" + name + R"(",)"
            R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
            R"( "initialSceneId": 0, "scenes": [ { "id": 0, "path": "Scenes/Main.scene" } ]})";
    }
}

bool RunProjectContentRootTests()
{
    using GameEngine::App::ProjectFile;

    // 흔한 모양: 콘텐츠가 Content/ 아래에 있다.
    TemporaryDirectory nested("content-root-nested");
    const std::filesystem::path nestedRoot = nested.GetPath();
    const bool wroteNested =
        WriteFile(nestedRoot / "Content" / "Game.gameproject", ProjectText("Game")) &&
        WriteFile(nestedRoot / "CMakeLists.txt", "");
    const bool findsNested =
        wroteNested && ProjectFile::FindContentRoot(nestedRoot) == nestedRoot / "Content";

    // 다른 모양: 콘텐츠가 프로젝트 루트에 .gameproject와 함께 있다. CMake 쪽이 CONTENT_DIR로
    // 이미 받아들이는 모양이고, 도구도 같은 답을 내야 한다.
    TemporaryDirectory flat("content-root-flat");
    const std::filesystem::path flatRoot = flat.GetPath();
    const bool wroteFlat =
        WriteFile(flatRoot / "Game.gameproject", ProjectText("Game")) &&
        WriteFile(flatRoot / "Scenes" / "Main.scene", R"({"sceneName": "Main", "gameObjects": []})");
    const bool findsFlat = wroteFlat && ProjectFile::FindContentRoot(flatRoot) == flatRoot;

    // 🔴 Content/가 있어도 그 안에 프로젝트 파일이 없으면 그것은 콘텐츠 루트가 아니다. 이름만
    // 보고 고르면 이 프로젝트는 「Content가 있는데 비어 있다」로 읽혀 열리지 않는다.
    TemporaryDirectory decoy("content-root-decoy");
    const std::filesystem::path decoyRoot = decoy.GetPath();
    const bool wroteDecoy =
        WriteFile(decoyRoot / "Content" / "notes.txt", "not a project") &&
        WriteFile(decoyRoot / "Game.gameproject", ProjectText("Game"));
    const bool ignoresDecoy = wroteDecoy && ProjectFile::FindContentRoot(decoyRoot) == decoyRoot;

    // 어느 자리에도 없으면 비어 있다. 부르는 쪽이 두 자리를 다 말해 줄 수 있도록.
    TemporaryDirectory empty("content-root-empty");
    const bool findsNothing = ProjectFile::FindContentRoot(empty.GetPath()).empty();

    return Expect(findsNested, "a project keeping its content in Content/ should be found there") &&
        Expect(findsFlat, "a project keeping its content at its root should be found there") &&
        Expect(
            ignoresDecoy,
            "a Content directory with no project file in it is not the content root") &&
        Expect(findsNothing, "a directory with no project file anywhere should answer nothing");
}

static const TestSupport::Registration gProjectContentRootTests{
    "AssetDatabase", "project content root tests should pass", RunProjectContentRootTests };
