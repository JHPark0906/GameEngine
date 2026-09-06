#include <filesystem>
#include <iostream>
#include <string>

#include "../GameEngine/App/ProjectFile.h"

#include "ProjectSourceRootTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    namespace fs = std::filesystem;

    [[nodiscard]] GameEngine::App::ProjectFileData ProjectAt(
        const fs::path& file, const fs::path& sourceRoot = {})
    {
        GameEngine::App::ProjectFileData project;
        project.filePath = file;
        project.settings.sourceRootPath = sourceRoot;
        return project;
    }
}

bool RunProjectSourceRootTests()
{
    std::cout << "running project source root tests\n";

    // ⑴ 코드가 어디 있는지는 물을 수 있다. 필드가 없으면 프로젝트 파일이 있는 자리이며,
    //    그것이 기존 프로젝트 파일이 한 글자도 바뀌지 않아도 되는 이유다.
    const fs::path beside = fs::path("C:") / "Games" / "Summit" / "Content";
    const bool sourceDefaultsBeside =
        ProjectAt(beside / "Summit.gameproject").GetSourceRootPath() == beside;

    // ⑵ SampleGame의 모양: 프로젝트 파일과 에셋은 Content/에 있고 코드는 그 위에 있다. 이
    //    필드가 있는 이유 전부가 이것이다 — 물어서 안 자리로 위를 가리킬 수 있어야, 프로젝트
    //    파일에서 위로 올라가며 CMakeLists.txt를 찾는 두 번째 탐색 규칙이 필요 없어진다.
    const fs::path repository = fs::path("C:") / "Games" / "SampleGame";
    const fs::path contentOfSample = repository / "Content";
    const bool sourceCanPointUp =
        ProjectAt(contentOfSample / "SampleGame.gameproject", "..").GetSourceRootPath() ==
        repository;

    // ⑶ 프로젝트 파일을 다시 쓸 때 이 필드가 살아남는가. 이 함수는 파일을 통째로 새로 쓰므로,
    //    적지 않으면 장면 하나를 더하는 것만으로 코드 자리가 사라지고 다음 빌드가 엉뚱한
    //    디렉터리를 CMake에게 넘긴다. 반대로 안 쓰던 프로젝트에는 줄이 생기지 않아야 한다.
    GameEngine::App::ProjectSettings withSource;
    withSource.projectName = L"SampleGame";
    withSource.sourceRootPath = "..";
    withSource.scenePaths.emplace(0u, "Scenes/Main.scene");
    const std::string writtenWithSource = GameEngine::App::ProjectFile::Serialize(withSource);

    GameEngine::App::ProjectSettings withoutSource = withSource;
    withoutSource.sourceRootPath.clear();
    const std::string writtenWithout = GameEngine::App::ProjectFile::Serialize(withoutSource);

    const bool rewriteKeepsTheField =
        writtenWithSource.find("\"sourceRootPath\": \"..\"") != std::string::npos;
    const bool rewriteAddsNothingByDefault =
        writtenWithout.find("sourceRootPath") == std::string::npos;

    return Expect(
            rewriteKeepsTheField,
            "rewriting a project should keep the source root it was written with") &&
        Expect(
            rewriteAddsNothingByDefault,
            "and should not add the field to a project that never had it") &&
        Expect(
            sourceDefaultsBeside,
            "a project that says nothing about its code should be read as keeping it beside the file") &&
        Expect(sourceCanPointUp, "and a project should be able to say its code is a level up");
}

static const TestSupport::Registration gProjectSourceRootTests{
    "AssetDatabase", "project source root tests should pass", RunProjectSourceRootTests };
