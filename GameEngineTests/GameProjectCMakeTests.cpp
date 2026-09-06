#include "GameProjectCMakeTests.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "App/ProjectFile.h"
#include "Build/CMakeLocation.h"
#include "Core/Json.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>이 파일은 &lt;repo&gt;/GameEngineTests/GameProjectCMakeTests.cpp에 있다.</summary>
    [[nodiscard]] std::filesystem::path RepositoryRoot()
    {
        return std::filesystem::path(__FILE__).parent_path().parent_path();
    }

    /// <summary>스키마 파일이 이름을 대는 컴포넌트 타입들이다. 읽지 못하면 비어 있다.</summary>
    /// <summary>그 파일이 마지막으로 쓰인 때다. 두 스키마가 얼마나 떨어져 있는지 보여 준다.</summary>
    [[nodiscard]] std::string WriteTimeOf(const std::filesystem::path& path)
    {
        std::error_code error;
        const std::filesystem::file_time_type when = std::filesystem::last_write_time(path, error);
        if (error)
        {
            return "unknown";
        }
        return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::floor<std::chrono::seconds>(when));
    }

    [[nodiscard]] std::vector<std::string> TypeNamesIn(const std::filesystem::path& schema)
    {
        std::vector<std::string> names;
        // 빌드가 다시 쓰는 파일은 TestSupport로 읽어 파일 부재와 일시적 교체 상태를 구분한다.
        const std::optional<std::string> contents = TestSupport::ReadFileWhenSettled(schema);
        if (!contents)
        {
            return names;
        }
        try
        {
            const GameEngine::Core::Json parsed = GameEngine::Core::Json::Parse(*contents);
            const GameEngine::Core::Json* const types =
                parsed.IsObject() ? parsed.Find("components") : nullptr;
            if (!types || !types->IsArray())
            {
                return names;
            }
            for (const GameEngine::Core::Json& entry : types->AsArray())
            {
                if (const GameEngine::Core::Json* const name =
                        entry.IsObject() ? entry.Find("type") : nullptr)
                {
                    names.push_back(name->Get<std::string>());
                }
            }
        }
        catch (const std::exception&)
        {
            names.clear();
        }
        return names;
    }

    /// <summary>
    /// 이 기계에서 부를 수 있는 <c>cmake</c>다. 엔진이 쥔 규칙을 그대로 부른다 — 시험이 자기
    /// 판을 따로 쓰면 도구가 고르는 cmake와 시험이 고르는 cmake가 갈라지고, 그러면 시험이
    /// 통과해도 도구가 도는 것을 보였다고 말할 수 없다.
    ///
    /// 찾지 못하면 비어 있고, 그때 시험은 건너뛰는 대신 실패한다 — 건너뛴 시험은 없는 시험에
    /// 가깝다.
    /// </summary>
    [[nodiscard]] std::filesystem::path FindCMake()
    {
        const std::optional<std::filesystem::path> found =
            GameEngine::Build::ResolveCMakePath({}, {});
        return found ? *found : std::filesystem::path();
    }

    /// <summary>
    /// 저장소를 한 번 configure하고 종료 코드를 돌려준다. 출력은 실패한 경우에만 읽는다 —
    /// 성공한 configure의 수십 줄은 시험 로그에 남길 값이 없다.
    /// </summary>
    [[nodiscard]] int Configure(
        const std::filesystem::path& cmake, const std::filesystem::path& binaryDirectory,
        const std::string& projectDirectory, std::string& output, const bool explicitProject = true)
    {
        std::vector<std::string> arguments{
            "-S", RepositoryRoot().string(), "-B", binaryDirectory.string(),
            "-DGAMEENGINE_OUTPUT_ROOT=" + (binaryDirectory / "x64").generic_string() };
        if (explicitProject)
        {
            arguments.push_back("-DGAMEEDITOR_PROJECT_DIRECTORY=" + projectDirectory);
        }
        const TestSupport::CommandResult result = TestSupport::RunCommand(cmake, arguments);
        output = result.output;
        return result.exitCode.value_or(-1);
    }
}

bool RunGameProjectComponentLinkTests()
{
    // CMake가 실제로 연결한 프로젝트의 타깃과 구성이 낸 스키마를 읽는다. 소스 트리의 스키마나
    // 다른 프로젝트의 오래된 스테이징 파일은 이 에디터의 등록 상태를 증명하지 못한다.
    const std::filesystem::path editorSchema = GAMEENGINE_TEST_EDITOR_SCHEMA;
    const std::filesystem::path projectSchema = GAMEENGINE_TEST_PROJECT_SCHEMA;

    const std::vector<std::string> editorTypes = TypeNamesIn(editorSchema);
    bool passed = Expect(
        !editorTypes.empty(),
        "the editor should have staged a component schema beside itself; build the editor first");

    // 프로젝트 없는 구성만 비교할 관계가 없다. 구성된 프로젝트의 스키마 누락은 실패다.
    if (projectSchema.empty())
    {
        std::cerr << "  the editor was configured without a game project; no project types to compare\n";
        return passed;
    }

    const std::vector<std::string> projectTypes = TypeNamesIn(projectSchema);
    if (!Expect(!projectTypes.empty(),
            "the configured game project must have a readable, nonempty staged component schema"))
    {
        std::cerr << "  configured project schema: " << projectSchema.string() << "\n";
        return false;
    }

    // 프로젝트가 자기 실행 파일에서 아는 타입은 에디터 안에서도 알려져 있어야 한다. 에디터가
    // 프로젝트의 오브젝트를 링크하고, 그 안의 정적 초기화자가 같은 문으로 등록하기 때문이다.
    // 정적 라이브러리였다면 링커가 그 오브젝트를 버려 이 관계가 조용히 깨진다.
    for (const std::string& name : projectTypes)
    {
        const bool known =
            std::ranges::find(editorTypes, name) != editorTypes.end();
        if (!known)
        {
            std::cerr << "  the editor does not know the project type: " << name << "\n"
                      << "  schemas from the configured editor and game project targets:\n"
                      << "    " << editorSchema.string() << " (written "
                      << WriteTimeOf(editorSchema) << ")\n"
                      << "    " << projectSchema.string() << " (written "
                      << WriteTimeOf(projectSchema) << ")\n";
        }
        passed &= Expect(
            known, "every component the game project declares is registered in the editor too");
    }
    return passed;
}

bool RunGameProjectConfigureTests()
{
    const std::filesystem::path cmake = FindCMake();
    if (!Expect(!cmake.empty(), "cmake should be on PATH or beside Visual Studio"))
    {
        return false;
    }

    TestSupport::TemporaryDirectory workspace("game-project-configure");
    const std::filesystem::path root = workspace.GetPath();
    bool passed = true;
    std::string output;

    const std::filesystem::path projectRoot = root / "fixture-project";
    const std::filesystem::path projectFile = projectRoot / "Content" / "ConfigureFixture.gameproject";
    if (!Expect(
            TestSupport::WriteFile(projectRoot / "CMakeLists.txt",
                "gameengine_add_game_project(ConfigureFixture SOURCES Source/Fixture.cpp)\n") &&
            TestSupport::WriteFile(projectRoot / "Source" / "Fixture.cpp",
                "int ProjectConfigureFixtureValue() { return 42; }\n") &&
            TestSupport::WriteFile(projectFile,
                R"({"projectName":"ConfigureFixture","sourceRootPath":"..",)"
                R"("window":{"width":1280,"height":720},"targetFrameRate":60,)"
                R"("initialSceneId":0,"scenes":[{"id":0,"path":"Scenes/Main.scene"}]})") &&
            TestSupport::WriteFile(projectRoot / "Content" / "Scenes" / "Main.scene",
                R"({"sceneName":"Main","gameObjects":[]})"),
            "the independent CMake project fixture must be written"))
    {
        return false;
    }

    {
        const int code = Configure(cmake, root / "valid", projectRoot.generic_string(), output);
        passed &= Expect(code == 0, "a project with a CMakeLists.txt configures");
        passed &= Expect(
            output.find("Play mode runs the components of ConfigureFixture") != std::string::npos,
            "and the configure identifies the explicitly selected project");
    }
    {
        const int code = Configure(cmake, root / "default", "", output, false);
        passed &= Expect(code == 0 && output.find("no game project directory") != std::string::npos,
            "default configuration must not select a game project from the source checkout");
    }

    // ⒝ 빈 값. 프로젝트를 아무도 대지 않은 것은 오류가 아니라 프로젝트 없는 에디터다.
    {
        const int code = Configure(cmake, root / "empty", "", output);
        passed &= Expect(code == 0, "an empty project directory is not an error");
        passed &= Expect(
            output.find("no game project directory") != std::string::npos,
            "and the configure says Play mode will have no project components");
    }

    // ⒞ 없는 경로. 오타이거나 옮겨진 디렉터리다.
    {
        const int code = Configure(
            cmake, root / "typo", (root / "no-such-project").generic_string(), output);
        passed &= Expect(code != 0, "a path that names nothing fails the configure");
        passed &= Expect(
            output.find("does not name a directory") != std::string::npos,
            "and says the path names no directory");
    }

    // 소스가 있는 프로젝트 디렉터리에 CMakeLists.txt가 없으면 원인을 진단해야 한다.
    {
        const std::filesystem::path incomplete = root / "no-cmakelists";
        std::filesystem::create_directories(incomplete / "Source");
        passed &= Expect(
            TestSupport::WriteFile(incomplete / "Source" / "Thing.cpp", "// a project's code\n"),
            "the incomplete project should be written");
        const int code = Configure(cmake, root / "incomplete", incomplete.generic_string(), output);
        passed &= Expect(code != 0, "a project directory with no CMakeLists.txt fails");
        passed &= Expect(
            output.find("has no CMakeLists.txt") != std::string::npos,
            "and names the file it is missing");
        // 무엇이 없는지만으로는 고칠 수 없다. 이 디렉터리는 프로젝트 파일의 한 필드에서 왔고,
        // 그 이름을 말하지 않으면 사람은 어디를 고쳐야 하는지 알 방법이 없다.
        passed &= Expect(
            output.find("sourceRootPath") != std::string::npos &&
                output.find(".gameproject") != std::string::npos,
            "and says which field of which file decided that directory");
    }

    // The descriptor in Content points at the source directory containing CMakeLists.txt.
    {
        const std::optional<GameEngine::App::ProjectFileData> project =
            GameEngine::App::ProjectFile::Load(projectFile);
        passed &= Expect(project.has_value(), "the generated project descriptor should load");
        if (project)
        {
            passed &= Expect(
                project->GetSourceRootPath() == projectRoot,
                "sourceRootPath should locate the project's own CMakeLists.txt");
            const int code = Configure(
                cmake, root / "from-field", project->GetSourceRootPath().generic_string(), output);
            passed &= Expect(code == 0, "configuring the sourceRootPath must succeed");
        }
    }
    return passed;
}

static const TestSupport::Registration gGameProjectComponentLinkTests{
    "ProjectBuilder", "game project component link tests should pass", RunGameProjectComponentLinkTests };

static const TestSupport::Registration gGameProjectConfigureTests{
    "ProjectBuilder", "game project configure tests should pass", RunGameProjectConfigureTests };
