#include "ProjectScriptTests.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "App/ProjectFile.h"
#include "Build/CMakeLocation.h"
#include "Rules/EditorScriptTemplate.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>이 파일은 &lt;repo&gt;/GameEngineTests/ProjectScriptTests.cpp에 있다.</summary>
    [[nodiscard]] std::filesystem::path RepositoryRoot()
    {
        return std::filesystem::path(__FILE__).parent_path().parent_path();
    }

    /// <summary>콘텐츠만 있는 프로젝트다. 코드를 갖기 전의 모습이며, 그것이 정상이다.</summary>
    [[nodiscard]] bool WriteContentOnlyProject(
        const std::filesystem::path& root, const std::string& name)
    {
        return TestSupport::WriteFile(
                   root / (name + ".gameproject"),
                   "{\n  \"projectName\": \"" + name + "\",\n"
                   "  \"initialSceneId\": 0,\n"
                   "  \"scenes\": [ { \"id\": 0, \"path\": \"Scenes/Main.scene\" } ]\n}\n") &&
            TestSupport::WriteFile(
                root / "Scenes" / "Main.scene",
                "{\n  \"sceneName\": \"Main\",\n  \"gameObjects\": []\n}\n");
    }
}

bool RunProjectScriptTests()
{
    using GameEngine::App::ProjectFile;
    TestSupport::TemporaryDirectory workspace("project-script");
    const std::filesystem::path root = workspace.GetPath();
    bool passed = true;

    // 이름 판정은 프로젝트 이름과 컴포넌트 이름이 같이 쓴다. 둘 다 CMake 타깃과 C++ 이름이
    // 되기 때문이다.
    passed &= Expect(
        ProjectFile::IsUsableAsIdentifier("Player") &&
            ProjectFile::IsUsableAsIdentifier("_hidden") &&
            ProjectFile::IsUsableAsIdentifier("Enemy2"),
        "an identifier is accepted");
    passed &= Expect(
        !ProjectFile::IsUsableAsIdentifier("") &&
            !ProjectFile::IsUsableAsIdentifier("2Fast") &&
            !ProjectFile::IsUsableAsIdentifier("My Component") &&
            !ProjectFile::IsUsableAsIdentifier("적군"),
        "a name that cannot be a C++ identifier is refused");
    passed &= Expect(
        !ProjectFile::IsUsableAsIdentifier("class") && !ProjectFile::IsUsableAsIdentifier("int"),
        "a reserved word is refused even though it looks like an identifier");

    // ⒜ 코드가 없던 프로젝트가 첫 컴포넌트를 얻는다. 이것이 이 기능이 있는 이유다: 자기
    // 저장소에 장면과 그림만 두고 있던 프로젝트가 여기서 빌드되는 프로젝트가 된다.
    const std::filesystem::path fresh = root / "Fresh";
    passed &= Expect(
        WriteContentOnlyProject(fresh, "Fresh"), "the content-only project should be written");
    passed &= Expect(
        !std::filesystem::exists(fresh / "CMakeLists.txt"),
        "and it starts with no build script, which is a project rather than a mistake");

    const std::optional<GameEditor::CreatedScript> created =
        GameEditor::CreateComponentScript(fresh, "Fresh", ".", fresh / "Source" / "Mover.h");
    passed &= Expect(created.has_value(), "a component can be created in it");
    if (created)
    {
        passed &= Expect(
            std::filesystem::exists(created->headerPath) &&
                std::filesystem::exists(created->sourcePath),
            "the pair is written into Source/");
        passed &= Expect(
            created->wroteBuildScript && std::filesystem::exists(fresh / "CMakeLists.txt"),
            "and the project gains a build script, because without one nothing compiles it");

        const std::string source = TestSupport::ReadFile(created->sourcePath);
        passed &= Expect(
            source.find("RegisterComponentType") != std::string::npos,
            "the component registers itself through the shared door");
        passed &= Expect(
            source.find("OBJECT") != std::string::npos,
            "and says why that registration may live in this translation unit");

        const std::string script = TestSupport::ReadFile(fresh / "CMakeLists.txt");
        passed &= Expect(
            script.find("gameengine_add_game_project(Fresh") != std::string::npos,
            "the build script declares the project under its own name");
        passed &= Expect(
            script.find("CONTENT_DIR \".\"") != std::string::npos,
            "and carries the content directory the descriptor named");
    }

    // ⒝ 같은 이름을 두 번은 거절한다. 사람이 쓴 컴포넌트를 템플릿으로 덮는 것은 되돌릴 수 없다.
    passed &= Expect(
        !GameEditor::CreateComponentScript(fresh, "Fresh", ".", fresh / "Source" / "Mover.h").has_value(),
        "the same name a second time is refused rather than overwriting");

    // ⒞ 이미 빌드 스크립트가 있는 프로젝트에는 짝만 더한다. 소스를 glob으로 모으므로 목록을
    // 고칠 일이 없고, 사람이 손댔을 수 있는 파일을 다시 쓸 이유도 없다.
    const std::string before = TestSupport::ReadFile(fresh / "CMakeLists.txt");
    const std::optional<GameEditor::CreatedScript> second =
        GameEditor::CreateComponentScript(fresh, "Fresh", ".", fresh / "Source" / "Spinner.h");
    passed &= Expect(second.has_value(), "a second component can be added");
    if (second)
    {
        passed &= Expect(
            !second->wroteBuildScript && TestSupport::ReadFile(fresh / "CMakeLists.txt") == before,
            "and the existing build script is left exactly as it was");
    }

    // ⒟ 쓸 수 없는 이름은 파일을 하나도 남기지 않는다.
    passed &= Expect(
        !GameEditor::CreateComponentScript(
             fresh, "Fresh", ".", fresh / "Source" / "class.h").has_value() &&
            !std::filesystem::exists(fresh / "Source" / "class.h"),
        "a refused name leaves nothing behind");

    // ⒠ 고른 자리가 Source/ 밖이면 거절한다. 빌드 스크립트가 그 디렉터리 하나만 훑으므로,
    // 다른 곳에 만들어 주면 파일은 생기는데 컴파일되지 않는다 — 사람이 보기에는 만들었는데
    // 컴포넌트가 나타나지 않는다.
    passed &= Expect(
        !GameEditor::CreateComponentScript(
             fresh, "Fresh", ".", fresh / "Elsewhere.h").has_value() &&
            !std::filesystem::exists(fresh / "Elsewhere.h"),
        "a place outside Source/ is refused and nothing is written there");

    // ⒡ 하위 폴더도 마찬가지다. glob이 재귀가 아니라 그 안의 파일은 빌드에 들어가지 않는다 —
    // 밖보다 오히려 알아채기 어려운 자리라 같은 이유로 거절한다.
    passed &= Expect(
        !GameEditor::CreateComponentScript(
             fresh, "Fresh", ".", fresh / "Source" / "Enemies" / "Slime.h").has_value() &&
            !std::filesystem::exists(fresh / "Source" / "Enemies" / "Slime.h"),
        "a subdirectory of Source/ is refused too, because the glob does not recurse");
    return passed;
}

bool RunProjectScriptConfigureTests()
{
    TestSupport::TemporaryDirectory workspace("project-script-configure");
    const std::filesystem::path root = workspace.GetPath();
    bool passed = true;

    const std::filesystem::path project = root / "Configured";
    passed &= Expect(
        WriteContentOnlyProject(project, "Configured"), "the project should be written");
    const std::optional<GameEditor::CreatedScript> created =
        GameEditor::CreateComponentScript(project, "Configured", ".", project / "Source" / "Mover.h");
    if (!Expect(created.has_value(), "the component should be created"))
    {
        return false;
    }

    // 파일을 놓는 것과 그것이 빌드에 들어가는 것은 다른 일이다. 여기서 실제로 configure해야
    // "만들어 두었는데 아무 빌드에도 안 들어간다"가 걸린다.
    //
    // cmake를 고르는 규칙은 엔진에 하나뿐이다. 여기서 다시 쓰면 이 시험이 도구와 다른 cmake로
    // 확인하게 된다.
    const std::optional<std::filesystem::path> cmake = GameEngine::Build::ResolveCMakePath({}, {});
    if (!Expect(cmake.has_value(), "cmake should be on PATH or beside Visual Studio"))
    {
        return false;
    }

    const TestSupport::CommandResult configure = TestSupport::RunCommand(
        *cmake,
        { "-S", RepositoryRoot().string(),
          "-B", (root / "build").string(),
          // 이 구성이 낳는 빌드는 진짜 컴파일과 링크를 한다. 산출물 루트를 이 시험의 트리로
          // 돌리지 않으면 그 빌드가 저장소의 x64/<Config>에 쓴다 — 방금 저장소를 지은 빌드가
          // 놓은 파일들을, 같은 ctest 실행의 다른 시험들이 읽는 동안.
          "-DGAMEENGINE_OUTPUT_ROOT=" + (root / "x64").generic_string(),
          "-DGAMEEDITOR_PROJECT_DIRECTORY=" + project.generic_string() });
    const int code = configure.exitCode.value_or(-1);
    const std::string output = configure.output;
    passed &= Expect(code == 0, "a project that just gained its first component configures");
    passed &= Expect(
        output.find("Play mode runs the components of Configured") != std::string::npos,
        "and the editor reports that it will run that project's components");
    if (code != 0)
    {
        std::cerr << output.substr(0, 1500) << "\n";
        return passed;
    }

    // configure가 통과해도 그 코드가 컴파일된다는 뜻은 아니다. 템플릿의 오타는 여기서만 걸린다.
    // 컴포넌트 오브젝트만 빌드한다 — 실행 파일을 링크할 이유는 없고, 재는 것은 생성된 소스가
    // 컴파일되는지 하나다.
    const TestSupport::CommandResult build = TestSupport::RunCommand(
        *cmake,
        { "--build", (root / "build").string(),
          "--target", "Configured_Components",
          "--config", "Debug" });
    const int buildCode = build.exitCode.value_or(-1);
    passed &= Expect(buildCode == 0, "and the component the editor wrote actually compiles");
    if (buildCode != 0)
    {
        std::cerr << build.output.substr(0, 2000) << "\n";
    }
    return passed;
}

static const TestSupport::Registration gProjectScriptTests{
    "ProjectBuilder", "project script tests should pass", RunProjectScriptTests };

static const TestSupport::Registration gProjectScriptConfigureTests{
    "ProjectBuilder", "project script configure tests should pass", RunProjectScriptConfigureTests };
