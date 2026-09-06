#include "RelativePathRuleTests.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>
    /// 규칙 밖에서 <c>lexically_relative</c>를 부르는 것이 허용된 파일들과, 그 이유다.
    ///
    /// 이유를 항목마다 적어 두는 것이 목록이 쓰레기통이 되지 않게 하는 유일한 방법이다. 이름만
    /// 있으면 다음 사람은 이것을 「여기 추가하면 되는 칸」으로 읽는다.
    /// </summary>
    constexpr std::array<std::pair<std::string_view, std::string_view>, 2> Allowed{
        std::pair{ std::string_view("GameEngine/Core/RelativePath.cpp"),
            std::string_view("the rule itself") },
        std::pair{ std::string_view("GameEngine/Platform/DirectoryContentSource.cpp"),
            std::string_view("it needs the canonical absolute path back, and resolving is on the "
                             "path of every asset read; asking the helper would canonicalise a "
                             "second time for a value it then throws away") },
    };
}

bool RunRelativePathRuleTests()
{
    namespace fs = std::filesystem;

    // 이 파일은 <repo>/GameEngineTests/RelativePathRuleTests.cpp에 있다.
    const fs::path repository = fs::path(__FILE__).parent_path().parent_path();
    std::error_code error;
    if (!fs::is_directory(repository, error))
    {
        std::cout << "  relative path rule test skipped: sources not found\n";
        return true;
    }

    constexpr std::array<std::string_view, 3> ScannedDirectories{
        "GameEngine", "GameEditor", "GameBuilder" };

    std::vector<std::string> violations;
    for (const std::string_view directory : ScannedDirectories)
    {
        for (const auto& entry : fs::recursive_directory_iterator(repository / directory, error))
        {
            if (error || !entry.is_regular_file())
            {
                continue;
            }
            const fs::path& path = entry.path();
            if (path.extension() != ".cpp" && path.extension() != ".h")
            {
                continue;
            }
            const std::string relativeText = fs::relative(path, repository, error).generic_string();
            const bool allowed = std::ranges::any_of(
                Allowed,
                [&relativeText](const auto& entry)
                {
                    return entry.first == relativeText;
                });
            if (allowed)
            {
                continue;
            }

            std::ifstream stream(path);
            std::string line;
            unsigned int lineNumber = 0;
            while (std::getline(stream, line))
            {
                ++lineNumber;
                const std::size_t first = line.find_first_not_of(" \t");
                if (first != std::string::npos && line.compare(first, 2, "//") == 0)
                {
                    // 주석이 규칙을 설명하려면 그 이름을 부를 수 있어야 한다.
                    continue;
                }
                if (line.find("lexically_relative") != std::string::npos)
                {
                    violations.push_back(
                        relativeText + ":" + std::to_string(lineNumber) +
                        " works out a path relative to a root by hand");
                }
            }
        }
    }

    for (const std::string& violation : violations)
    {
        std::cerr << "  relative path: " << violation << "\n";
    }
    return Expect(
        violations.empty(),
        "a path relative to a root should come from Core::RelativePathWithin, not from "
        "lexically_relative at the call site");
}

static const TestSupport::Registration gRelativePathRuleTests{
    "Layering", "relative path rule tests should pass", RunRelativePathRuleTests };
