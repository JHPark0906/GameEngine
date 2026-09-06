#include "TestRegistrationTests.h"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::ReadFile;

namespace
{
    [[nodiscard]] std::filesystem::path TestsRoot()
    {
        // 이 파일은 <repo>/GameEngineTests/TestRegistrationTests.cpp에 있다.
        return std::filesystem::path(__FILE__).parent_path();
    }

    /// <summary>등록 줄 하나의 모양이다. 세 번째 인자가 그 시험의 진입점이다.</summary>
    const std::regex RegistrationPattern{
        R"raw(Registration\s+g\w+\{\s*"[^"]+"\s*,\s*"[^"]+"\s*,\s*(Run\w+)\s*\})raw" };

    /// <summary>헤더가 내건 진입점의 모양이다.</summary>
    const std::regex EntryPointPattern{ R"raw(bool\s+(Run\w+)\(\)\s*;)raw" };

    [[nodiscard]] std::vector<std::string> MatchesIn(
        const std::string& text, const std::regex& pattern)
    {
        std::vector<std::string> names;
        for (std::sregex_iterator match(text.begin(), text.end(), pattern), end;
            match != end; ++match)
        {
            names.push_back((*match)[1].str());
        }
        return names;
    }
}

bool RunTestRegistrationTests()
{
    std::vector<std::string> registeredInSource;
    std::multiset<std::string> declaredEntryPoints;
    for (const std::filesystem::directory_entry& entry :
        std::filesystem::directory_iterator(TestsRoot()))
    {
        const std::filesystem::path& path = entry.path();
        if (path.extension() == ".cpp")
        {
            for (std::string& name : MatchesIn(ReadFile(path), RegistrationPattern))
            {
                registeredInSource.push_back(std::move(name));
            }
        }
        else if (path.extension() == ".h")
        {
            for (std::string& name : MatchesIn(ReadFile(path), EntryPointPattern))
            {
                declaredEntryPoints.insert(std::move(name));
            }
        }
    }
    const std::set<std::string> registeredNames(
        registeredInSource.begin(), registeredInSource.end());

    // 헤더가 내건 진입점마다 등록이 있어야 한다. 없으면 그 시험은 아무 소리 없이 돌지 않는다.
    bool everyEntryPointIsRegistered = true;
    for (const std::string& entryPoint : declaredEntryPoints)
    {
        if (!registeredNames.contains(entryPoint))
        {
            std::cerr << "FAILED: this test is declared but never registers itself. test="
                      << entryPoint << '\n';
            everyEntryPointIsRegistered = false;
        }
    }

    // 같은 진입점을 두 번 등록하면 그 시험이 두 번 돈다. 실패가 두 줄로 보이는 것도 문제지만,
    // 두 스위트에 나뉘어 등록되면 어느 쪽이 그것을 덮는지가 이름만으로는 답이 없다.
    const bool nothingRegistersTwice = registeredNames.size() == registeredInSource.size();

    // 🔴 원본에 적힌 수와 실행 시 등록부의 크기가 같아야 한다. 어긋나면 링커가 등록 객체를
    // 버린 것이고, 버려진 시험은 실패하지 않으므로 이 비교 말고는 그것을 말해 주는 것이 없다.
    const std::size_t registeredAtRuntime = TestSupport::RegisteredTests().size();
    const bool everyRegistrationSurvivedLinking =
        registeredAtRuntime == registeredInSource.size();
    if (!everyRegistrationSurvivedLinking)
    {
        std::cerr << "FAILED: registrations in the source and at runtime disagree. source="
                  << registeredInSource.size() << ", runtime=" << registeredAtRuntime << '\n';
    }

    // 등록부의 항목은 전부 부를 수 있어야 한다. null은 등록만 되고 부를 것이 없는 상태다.
    bool everyRegistrationIsCallable = !TestSupport::RegisteredTests().empty();
    for (const TestSupport::RegisteredTest& test : TestSupport::RegisteredTests())
    {
        everyRegistrationIsCallable = everyRegistrationIsCallable && test.run != nullptr &&
            !test.suite.empty() && !test.description.empty();
    }

    return Expect(
            everyEntryPointIsRegistered,
            "every test a header declares should register itself") &&
        Expect(nothingRegistersTwice, "no test should register itself twice") &&
        Expect(
            everyRegistrationSurvivedLinking,
            "every registration written in the source should be there at runtime") &&
        Expect(
            everyRegistrationIsCallable,
            "every registration should name a suite, a description and something to call");
}

static const TestSupport::Registration gTestRegistrationTests{
    "Core", "test registration tests should pass", RunTestRegistrationTests };
