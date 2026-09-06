#include "CMakeLocationTests.h"

#include <filesystem>
#include <iostream>
#include <string>

#include "Build/CMakeLocation.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>
    /// 캐시 한 장을 놓는다. 실제 캐시는 수백 줄이지만 이 규칙이 읽는 것은 줄 하나라, 그 줄이
    /// 다른 줄들 사이에 있을 때도 찾아지는지를 함께 본다.
    /// </summary>
    [[nodiscard]] bool WriteCache(
        const std::filesystem::path& directory, const std::string& cmakeCommand)
    {
        std::string contents =
            "# This is the CMakeCache file.\n"
            "CMAKE_BUILD_TYPE:STRING=\n"
            "GAMEENGINE_OUTPUT_ROOT:PATH=C:/somewhere/x64\n";
        if (!cmakeCommand.empty())
        {
            contents += "CMAKE_COMMAND:INTERNAL=" + cmakeCommand + "\n";
        }
        contents += "CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026\n";
        return TestSupport::WriteFile(directory / "CMakeCache.txt", contents);
    }
}

bool RunCMakeLocationTests()
{
    TestSupport::TemporaryDirectory workspace("cmake-location");
    const std::filesystem::path root = workspace.GetPath();
    bool passed = true;

    // 캐시가 가리키는 파일이 실제로 있어야 그것을 쓴다. 이 시험은 그 자리에 아무 실행 파일이나
    // 하나 놓고 그 경로가 선택되는지만 본다 — 무엇을 실행하는지는 여기서 볼 일이 아니다.
    const std::filesystem::path pretendCMake = root / "tree-cmake" / "cmake.exe";
    passed &= Expect(
        TestSupport::WriteFile(pretendCMake, "not a real program"),
        "the stand-in cmake should be written");

    // ⒜ 캐시에 적힌 것이 있으면 그것이다. 이 트리를 만든 cmake가 이 트리를 읽을 수 있는
    // 유일한 답이라, PATH에 다른 cmake가 있어도 그쪽으로 가지 않는다.
    {
        const std::filesystem::path tree = root / "configured";
        std::filesystem::create_directories(tree);
        passed &= Expect(
            WriteCache(tree, pretendCMake.generic_string()), "the cache should be written");

        const std::optional<std::filesystem::path> resolved =
            GameEngine::Build::ResolveCMakePath({}, tree);
        passed &= Expect(resolved.has_value(), "a configured tree answers with a cmake");
        passed &= Expect(
            resolved && std::filesystem::equivalent(*resolved, pretendCMake),
            "and it is the one the cache records, not whatever is first on PATH");
    }

    // ⒝ 캐시가 없으면 다음 차례로 내려간다. 이 기계에는 Visual Studio나 PATH의 cmake가 있으므로
    // 답이 나오고, 그 답은 캐시가 가리키던 것과 다르다.
    {
        const std::filesystem::path tree = root / "unconfigured";
        std::filesystem::create_directories(tree);

        const std::optional<std::filesystem::path> resolved =
            GameEngine::Build::ResolveCMakePath({}, tree);
        const bool fellThrough =
            !resolved || !std::filesystem::equivalent(*resolved, pretendCMake);
        passed &= Expect(
            fellThrough, "a tree with no cache does not answer with the other tree's cmake");
        if (resolved)
        {
            std::cerr << "  without a cache the next answer is " << resolved->string() << "\n";
        }
    }

    // ⒞ 캐시는 있는데 그 항목이 없는 경우도 같다. 오래된 트리나 다른 도구가 만든 트리다.
    {
        const std::filesystem::path tree = root / "no-entry";
        std::filesystem::create_directories(tree);
        passed &= Expect(WriteCache(tree, {}), "a cache without the entry should be written");
        passed &= Expect(
            !GameEngine::Build::ReadCacheEntry(tree, "CMAKE_COMMAND:INTERNAL=").has_value(),
            "an absent entry reads as nothing rather than as an empty path");
    }

    // ⒟ 캐시가 가리키는 파일이 사라졌으면 그 답은 못 쓴다. 트리를 만든 cmake가 지워지거나
    // 옮겨진 경우이고, 없는 파일을 실행하려 드는 것보다 다음 차례로 가는 편이 낫다.
    {
        const std::filesystem::path tree = root / "stale";
        std::filesystem::create_directories(tree);
        passed &= Expect(
            WriteCache(tree, (root / "gone" / "cmake.exe").generic_string()),
            "the stale cache should be written");

        const std::optional<std::filesystem::path> resolved =
            GameEngine::Build::ResolveCMakePath({}, tree);
        const bool refused =
            !resolved || resolved->filename() != std::filesystem::path("cmake.exe") ||
            std::filesystem::exists(*resolved);
        passed &= Expect(refused, "a cache pointing at a missing file does not answer with it");
    }

    // ⒠ 사람이 이름을 대면 그것이 이긴다. 나머지 셋이 틀렸을 때 쓰는 길이다.
    {
        const std::filesystem::path tree = root / "configured";
        const std::filesystem::path requested = root / "asked-for" / "cmake.exe";
        passed &= Expect(
            TestSupport::WriteFile(requested, "not a real program either"),
            "the requested cmake should be written");

        const std::optional<std::filesystem::path> resolved =
            GameEngine::Build::ResolveCMakePath(requested, tree);
        passed &= Expect(
            resolved && std::filesystem::equivalent(*resolved, requested),
            "an explicit request wins over the cache");
    }

    // 캐시 읽기 자체도 한 번 본다. 이 도구가 출력 루트를 알아내는 것도 같은 함수를 지난다.
    {
        const std::filesystem::path tree = root / "configured";
        const std::optional<std::string> outputRoot =
            GameEngine::Build::ReadCacheEntry(tree, "GAMEENGINE_OUTPUT_ROOT:PATH=");
        passed &= Expect(
            outputRoot && *outputRoot == "C:/somewhere/x64",
            "the same reader answers for the entry the build tree defines");
    }
    return passed;
}

static const TestSupport::Registration gCMakeLocationTests{
    "ProjectBuilder", "cmake location tests should pass", RunCMakeLocationTests };
