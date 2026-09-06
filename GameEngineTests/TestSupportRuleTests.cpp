#include "TestSupportRuleTests.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>
    /// 시험 파일이 자기 것을 다시 만들면 안 되는 이름들이다.
    ///
    /// 이름만 겹치는 것을 잡는 것이 아니라 <b>정의</b>를 잡는다. 부르는 것은 당연히 있어야 하고,
    /// 여기서 붉어져야 하는 것은 옆 파일에서 복사해 온 두 번째 구현이다.
    /// </summary>
    const std::array<std::pair<std::string_view, std::regex>, 2> ForbiddenDefinitions{
        std::pair{ std::string_view("Expect"),
            std::regex{ R"(^\s*(\[\[nodiscard\]\]\s*)?bool\s+Expect\s*\()" } },
        std::pair{ std::string_view("a whole-file reader"),
            std::regex{
                R"(^\s*(\[\[nodiscard\]\]\s*)?std::string\s+Read[A-Za-z]*\s*\(\s*const\s+std::filesystem)" } },
    };
}

bool RunTestSupportRuleTests()
{
    namespace fs = std::filesystem;

    // 이 파일은 <repo>/GameEngineTests/TestSupportRuleTests.cpp에 있다.
    const fs::path directory = fs::path(__FILE__).parent_path();
    std::error_code error;
    if (!fs::is_directory(directory, error))
    {
        std::cout << "  test support rule test skipped: sources not found\n";
        return true;
    }

    std::vector<std::string> violations;
    for (const auto& entry : fs::directory_iterator(directory, error))
    {
        if (error || !entry.is_regular_file() || entry.path().extension() != ".cpp")
        {
            continue;
        }
        // TestSupport 자신이 그것들을 정의하는 자리다.
        if (entry.path().filename() == "TestSupport.cpp")
        {
            continue;
        }
        std::ifstream stream(entry.path());
        std::string line;
        unsigned int lineNumber = 0;
        while (std::getline(stream, line))
        {
            ++lineNumber;
            for (const auto& [name, pattern] : ForbiddenDefinitions)
            {
                if (std::regex_search(line, pattern))
                {
                    violations.push_back(
                        entry.path().filename().string() + ":" + std::to_string(lineNumber) +
                        " defines its own " + std::string(name));
                }
            }
        }
    }

    for (const std::string& violation : violations)
    {
        std::cerr << "  test support: " << violation << "\n";
    }
    return Expect(
        violations.empty(),
        "a test file should use TestSupport's Expect and ReadFile rather than define its own");
}

static const TestSupport::Registration gTestSupportRuleTests{
    "Layering", "test support rule tests should pass", RunTestSupportRuleTests };
