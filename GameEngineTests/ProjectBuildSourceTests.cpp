#include "ProjectBuildSourceTests.h"

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>

#include "Build/ProjectBuildSource.h"
#include "TestSupport.h"

namespace
{
    std::string Utf8Path(const std::filesystem::path& path)
    {
        const std::u8string text = path.generic_u8string();
        return std::string(text.begin(), text.end());
    }
}

bool RunProjectBuildSourceTests()
{
    namespace fs = std::filesystem;
    using GameEngine::Build::ValidateProjectBuildSource;
    using TestSupport::Expect;
    using TestSupport::WriteFile;
    TestSupport::TemporaryDirectory temporary("project-build-source");
    const fs::path root = temporary.GetPath();
    const fs::path first = root / "first checkout" / "SameGame";
    const fs::path second = root / "second checkout" / "SameGame";
    const fs::path tree = root / "engine-build";
    if (!Expect(WriteFile(first / "CMakeLists.txt", "first source") &&
            WriteFile(second / "CMakeLists.txt", "second source"),
            "two checkouts with the same game target should exist")) return false;
    std::string message;
    const auto writeSource = [&](const std::string_view value)
    {
        return WriteFile(tree / "CMakeCache.txt",
            "CMAKE_HOME_DIRECTORY:INTERNAL=" + Utf8Path(root) + "\n"
            "GAMEENGINE_PROJECT_SOURCE_SameGame:INTERNAL=" + std::string(value) + "\n");
    };
    bool passed = Expect(writeSource(Utf8Path(first)), "target source metadata should be written");
    passed = Expect(ValidateProjectBuildSource(tree, "SameGame", first, message) && message.empty(),
        "the target source must match even when the top-level CMake source is the engine") && passed;
    passed = Expect(!ValidateProjectBuildSource(tree, "SameGame", second, message) && !message.empty(),
        "another checkout with the same directory and target name must be rejected") && passed;
    passed = Expect(ValidateProjectBuildSource(tree, "SameGame", first / "." / ".." / "SameGame" / ".", message),
        "a canonical alias of the same source directory must be accepted") && passed;
    passed = Expect(!ValidateProjectBuildSource(tree, "OtherGame", first, message) && !message.empty(),
        "a target without its own source metadata must be rejected") && passed;
    passed = Expect(!ValidateProjectBuildSource(root / "no-build-tree", "SameGame", first, message),
        "a missing build cache must fail closed") && passed;

    passed = Expect(WriteFile(tree / "CMakeCache.txt", "CMAKE_HOME_DIRECTORY:INTERNAL=" + Utf8Path(first)),
        "an older cache without target metadata should be written") && passed;
    passed = Expect(!ValidateProjectBuildSource(tree, "SameGame", first, message) &&
        message.find("Configure") != std::string::npos,
        "an older cache must request configuration rather than guessing from CMAKE_HOME_DIRECTORY") && passed;

    for (const std::string& invalid : { std::string(), std::string("../SameGame"),
            Utf8Path(root / "missing-source"), Utf8Path(first / "CMakeLists.txt"),
            std::string(1, '\xff'), Utf8Path(first) + std::string("\0suffix", 7) })
    {
        passed = Expect(writeSource(invalid), "invalid source metadata should be written") && passed;
        passed = Expect(!ValidateProjectBuildSource(tree, "SameGame", first, message) && !message.empty(),
            "missing, non-directory, or malformed recorded sources must be rejected") && passed;
    }
    const fs::path unicodeProject = root / fs::path(u8"프로젝트 경로") / "SameGame";
    passed = Expect(WriteFile(unicodeProject / "CMakeLists.txt", "unicode source") &&
        writeSource(Utf8Path(unicodeProject)), "UTF-8 source metadata should be written") && passed;
    passed = Expect(ValidateProjectBuildSource(tree, "SameGame", unicodeProject, message),
        "CMake UTF-8 paths must match the native project path") && passed;
    passed = Expect(!ValidateProjectBuildSource(tree, "SameGame", root / "missing-project", message),
        "a removed requested source directory must be rejected") && passed;
    passed = Expect(!ValidateProjectBuildSource(tree, "SameGame:INTERNAL=ignored", unicodeProject, message),
        "a target name must not inject another cache key") && passed;
    const fs::path repository = fs::path(__FILE__).parent_path().parent_path();
    passed = Expect(writeSource(Utf8Path(repository)), "the workspace's source path should be recorded") && passed;
    passed = Expect(ValidateProjectBuildSource(tree, "SameGame", repository / ".", message),
        "the source volume must work even when its filesystem does not support file identity queries") && passed;
    return passed;
}

static const TestSupport::Registration gProjectBuildSourceTests{
    "ProjectBuilder", "game target source must match the requested project", RunProjectBuildSourceTests };
