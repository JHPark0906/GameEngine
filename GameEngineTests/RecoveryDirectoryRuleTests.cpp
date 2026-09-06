#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "RecoveryDirectoryRuleTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

bool RunRecoveryDirectoryRuleTests()
{
    std::cout << "running recovery directory rule tests\n";

    // 이 파일은 <repo>/GameEngineTests/RecoveryDirectoryRuleTests.cpp에 있다.
    const std::filesystem::path testDirectory = std::filesystem::path(__FILE__).parent_path();

    std::vector<std::string> offenders;
    std::size_t read = 0;
    std::error_code error;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(testDirectory, error))
    {
        if (!entry.is_regular_file(error) || entry.path().extension() != ".cpp")
        {
            continue;
        }
        // 이 파일 자신은 그 이름을 문자열로 들고 있어야 하므로 세지 않는다.
        if (entry.path().filename() == "RecoveryDirectoryRuleTests.cpp")
        {
            continue;
        }
        std::ifstream file(entry.path());
        if (!file)
        {
            continue;
        }
        ++read;
        for (std::string line; std::getline(file, line);)
        {
            if (line.find("GetRecoveryDirectory") != std::string::npos)
            {
                offenders.push_back(entry.path().filename().string());
                break;
            }
        }
    }

    // 소스를 하나도 읽지 못했다면 이 시험은 아무것도 재지 않은 것이다. 그 상태로 초록이면
    // 규칙이 지켜지는지 아무도 모르는 채 지나간다.
    bool passed = Expect(read > 0, "the rule test should have test sources to read");
    passed = Expect(
        offenders.empty(),
        "no test should name GetRecoveryDirectory; the real snapshots are the user's work")
        && passed;
    for (const std::string& name : offenders)
    {
        std::cout << "  reaches for the real recovery directory: " << name << "\n";
    }
    return passed;
}

static const TestSupport::Registration gRecoveryDirectoryRuleTests{
    "EditorDocument", "recovery directory rule tests should pass", RunRecoveryDirectoryRuleTests };
