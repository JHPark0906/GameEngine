#include "ProjectBuildCommandTests.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "Rules/EditorProjectBuild.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    [[nodiscard]] bool Contains(
        const std::vector<std::string>& arguments, const std::string& value)
    {
        return std::ranges::find(arguments, value) != arguments.end();
    }

    /// <summary>
    /// 어떤 인자 바로 뒤에 오는 값이다. cmake의 스위치들은 값을 뒤에 두므로, 순서가 맞는지를
    /// 이것으로 본다 — 값이 목록 어딘가에 있기만 해서는 명령이 되지 않는다.
    /// </summary>
    [[nodiscard]] std::string ValueAfter(
        const std::vector<std::string>& arguments, const std::string& option)
    {
        const auto found = std::ranges::find(arguments, option);
        if (found == arguments.end() || found + 1 == arguments.end())
        {
            return {};
        }
        return *(found + 1);
    }

    [[nodiscard]] GameEditor::EditorBuildTree SampleTree()
    {
        GameEditor::EditorBuildTree tree;
        tree.sourceRoot = "C:/work/GameEngine";
        tree.buildDirectory = "C:/work/GameEngine/build/vs";
        tree.configuration = "Debug";
        return tree;
    }
}

bool RunProjectBuildCommandTests()
{
    using namespace GameEditor;
    bool passed = true;

    const std::filesystem::path cmakePath = "C:/tools/cmake.exe";
    const std::filesystem::path projectRoot = "D:/games/Summit";

    // ⑴ configure는 이 에디터가 나온 트리를 다시 만든다. 다른 트리를 만들면 컴파일은 성공하고
    // 에디터는 그대로여서, 사람은 빌드가 되는데 아무것도 달라지지 않는 것을 보게 된다.
    {
        const GameEngine::Platform::ProcessRequest request =
            MakeConfigureRequest(cmakePath, SampleTree(), projectRoot);
        passed &= Expect(request.executable == cmakePath, "configure runs the cmake it was given");
        passed &= Expect(
            ValueAfter(request.arguments, "-S") == "C:/work/GameEngine",
            "and configures from the tree's own source root");
        passed &= Expect(
            ValueAfter(request.arguments, "-B") == "C:/work/GameEngine/build/vs",
            "into the build directory this editor came out of");
    }

    // ⑵ 열린 프로젝트를 트리에 알린다. 이 인자가 없으면 트리는 여전히 예전 프로젝트를 알고
    // 있고, 빌드는 사람이 열어 둔 것이 아닌 다른 프로젝트를 컴파일한다.
    {
        const GameEngine::Platform::ProcessRequest request =
            MakeConfigureRequest(cmakePath, SampleTree(), projectRoot);
        passed &= Expect(
            Contains(request.arguments, "-DGAMEEDITOR_PROJECT_DIRECTORY=D:/games/Summit"),
            "configure points the tree at the project that is open");
    }

    // ⑶ 컴파일은 타깃 하나만 짓는다. 트리 전체를 지으면 에디터 자신도 다시 지어지는데, 그
    // 실행 파일은 지금 돌고 있어 링커가 쓸 수 없다.
    {
        const GameEngine::Platform::ProcessRequest request =
            MakeBuildRequest(cmakePath, SampleTree(), "Summit");
        passed &= Expect(
            ValueAfter(request.arguments, "--build") == "C:/work/GameEngine/build/vs",
            "the build step builds the same tree");
        passed &= Expect(
            ValueAfter(request.arguments, "--target") == "Summit",
            "and only the project's own target");
        passed &= Expect(
            ValueAfter(request.arguments, "--config") == "Debug",
            "in the configuration this editor was built in");
    }

    // ⑷ 트리를 모르는 에디터는 빌드하지 않는다. CMake가 심어 주는 값이라, 다른 방법으로 만든
    // 에디터에는 없다. 없는 값으로 cmake를 부르면 엉뚱한 디렉터리를 트리로 삼는다.
    {
        EditorBuildTree unknown;
        passed &= Expect(!unknown.IsKnown(), "an editor with no build tree knows it");

        EditorBuildTree partial = SampleTree();
        partial.configuration.clear();
        passed &= Expect(
            !partial.IsKnown(), "and so does one that knows everything but the configuration");

        passed &= Expect(SampleTree().IsKnown(), "while a complete tree is usable");

        ProjectBuild build;
        passed &= Expect(
            !build.Start(unknown, "D:/games/Summit", "Summit"),
            "starting a build without a tree is refused");
        passed &= Expect(!build.IsRunning(), "and leaves nothing running");
    }

    // ⑸ 이름 없는 프로젝트도 거절한다. 빈 타깃 이름은 cmake에게 「모두 지어라」로 읽힌다.
    {
        ProjectBuild build;
        passed &= Expect(
            !build.Start(SampleTree(), "D:/games/Summit", ""),
            "a build with no target name is refused");
    }
    return passed;
}

static const TestSupport::Registration gProjectBuildCommandTests{
    "ProjectBuilder", "project build command tests should pass", RunProjectBuildCommandTests };
