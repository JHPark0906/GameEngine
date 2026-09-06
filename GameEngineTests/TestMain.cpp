#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "Benchmarks.h"
#include "FbxSkeletalImportTests.h"
#include "TestSupport.h"

namespace
{
    /// <summary>
    /// CTest 스위트의 이름과 순서이며 --suite 인자와 GameEngineTests/CMakeLists.txt 목록에 일치해야 한다.
    /// SuiteList가 --expect-suites를 통해 두 목록을 비교한다. 개별 시험은 자기 파일에서 등록한다.
    /// ShaderContract가 셰이더 아티팩트를 준비한 뒤 BackendImage 장치가 이를 사용해야 한다.
    /// CTest는 같은 순서를 DEPENDS로 선언한다.
    /// </summary>
    constexpr std::array SuiteNames{
        std::string_view("AssetDatabase"),
        std::string_view("RenderFrame"),
        std::string_view("Physics"),
        std::string_view("RenderCache"),
        std::string_view("ShaderContract"),
        std::string_view("ContentSource"),
        std::string_view("RuntimeObject"),
        std::string_view("Animator"),
        std::string_view("Audio"),
        std::string_view("AppTiming"),
        std::string_view("EditorSettings"),
        std::string_view("EditorDocument"),
        std::string_view("Diagnostics"),
        std::string_view("Math"),
        std::string_view("PoseSampler"),
        std::string_view("Core"),
        std::string_view("UIModel"),
        std::string_view("Layering"),
        std::string_view("UIContext"),
        std::string_view("UILayout"),
        std::string_view("UIEvent"),
        std::string_view("ProjectBuilder"),
        std::string_view("BackendImage"),
        std::string_view("SkinnedMeshRender"),
        std::string_view("PlayerStartup"),
        std::string_view("FbxSkeletalImport"),
    };

    /// <summary>
    /// 한 스위트에 등록된 시험들이다. <b>설명 문자열로 정렬해서</b> 돌려준다: 등록 순서는
    /// 번역 단위의 링크 순서라 빌드가 정할 일이고, 시험이 도는 순서가 그것을 따라 흔들리면
    /// 실패를 재현하는 일이 빌드를 재현하는 일이 된다.
    /// </summary>
    [[nodiscard]] std::vector<TestSupport::RegisteredTest> TestsOf(const std::string_view suite)
    {
        std::vector<TestSupport::RegisteredTest> tests;
        for (const TestSupport::RegisteredTest& test : TestSupport::RegisteredTests())
        {
            if (test.suite == suite)
            {
                tests.push_back(test);
            }
        }
        std::ranges::sort(tests, {}, &TestSupport::RegisteredTest::description);
        return tests;
    }

    /// <summary>
    /// 어느 스위트에도 속하지 않는 등록이다. 스위트 이름을 잘못 적은 시험은 조용히 돌지 않게
    /// 되므로, 그것을 실패로 만든다.
    /// </summary>
    [[nodiscard]] std::vector<TestSupport::RegisteredTest> TestsWithNoSuite()
    {
        std::vector<TestSupport::RegisteredTest> orphans;
        for (const TestSupport::RegisteredTest& test : TestSupport::RegisteredTests())
        {
            if (std::ranges::find(SuiteNames, test.suite) == SuiteNames.end())
            {
                orphans.push_back(test);
            }
        }
        return orphans;
    }

    [[nodiscard]] bool ReportOrphans()
    {
        const std::vector<TestSupport::RegisteredTest> orphans = TestsWithNoSuite();
        for (const TestSupport::RegisteredTest& test : orphans)
        {
            std::cerr << "FAILED: this test names a suite that does not exist. suite=" << test.suite
                      << ", test=" << test.description << '\n';
        }
        return orphans.empty();
    }

    [[nodiscard]] bool RunSuite(const std::string_view suite)
    {
        bool passed = true;
        for (const TestSupport::RegisteredTest& test : TestsOf(suite))
        {
            if (!test.run())
            {
                std::cerr << "FAILED: " << suite << ": " << test.description << '\n';
                passed = false;
            }
        }
        return passed;
    }

    void PrintUsage()
    {
        std::cerr << "usage: GameEngineTests [--list | --suite <name> | --expect-suites <name>... |\n"
                     "                        --benchmark <content path> [scale...] |\n"
                     "                        --measure <content path> [assetCount...] |\n"
                     "                        --external-skeletal-fbx <path>]\n"
                     "available suites:\n";
        for (const std::string_view suite : SuiteNames)
        {
            std::cerr << "  " << suite << '\n';
        }
    }
}

int main(const int argc, char** const argv)
{
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);

    if (!arguments.empty() && arguments[0] == "--external-skeletal-fbx")
    {
        if (arguments.size() != 2)
        {
            PrintUsage();
            return 1;
        }
        return RunExternalFbxSkeletalImportTests(std::filesystem::path(arguments[1])) ? 0 : 1;
    }

    if (!arguments.empty() && arguments[0] == "--benchmark")
    {
        // 성능 측정은 CTest 밖의 수동 도구다: 시간 단언은 머신 의존이라 여기 두지 않는다.
        // 실행 방법은 Docs/VALIDATION.md에 있으며 측정 환경과 결과를 함께 기록한다.
        return RunBenchmarks({ arguments.begin() + 1, arguments.end() }) ? 0 : 1;
    }

    if (!arguments.empty() && arguments[0] == "--measure")
    {
        // 핫스팟 측정도 같은 이유로 CTest 밖이다. 벤치마크가 "얼마나 걸리나"를 재는 반면,
        // 이쪽은 "최적화에 착수할 가치가 있나"를 판단할 분해 수치를 낸다.
        return RunHotspotMeasurements({ arguments.begin() + 1, arguments.end() }) ? 0 : 1;
    }

    if (!arguments.empty() && arguments[0] == "--registered")
    {
        // 등록된 시험 수다. 원본에 적힌 등록 줄 수와 이 값을 비교하는 시험이 있고, 그 둘이
        // 어긋나는 것이 링커가 등록을 버렸다는 뜻이다.
        std::cout << TestSupport::RegisteredTests().size() << '\n';
        return 0;
    }

    if (!arguments.empty() && arguments[0] == "--list")
    {
        for (const std::string_view suite : SuiteNames)
        {
            std::cout << suite << '\n';
        }
        return 0;
    }

    if (!arguments.empty() && arguments[0] == "--expect-suites")
    {
        // CMake가 등록한 스위트 목록이 이 목록과 정확히 일치하는지 검사한다. 스위트를
        // 추가하고 CMake 목록을 잊으면, 그 스위트는 CTest에서 조용히 빠진다 — 이 비교가 그
        // 누락을 테스트 실패로 바꾼다.
        const std::span<const std::string_view> expected(arguments.begin() + 1, arguments.end());
        bool matches = expected.size() == SuiteNames.size();
        for (std::size_t index = 0; matches && index < expected.size(); ++index)
        {
            matches = expected[index] == SuiteNames[index];
        }
        if (!matches)
        {
            std::cerr << "FAILED: the CMake suite list does not match the registered suites.\n"
                         "registered:";
            for (const std::string_view suite : SuiteNames)
            {
                std::cerr << ' ' << suite;
            }
            std::cerr << "\nexpected by CMake:";
            for (const std::string_view name : expected)
            {
                std::cerr << ' ' << name;
            }
            std::cerr << '\n';
            return 1;
        }
        // 이름이 맞아도 어느 스위트에도 닿지 않는 등록이 있으면 그 시험은 돌지 않는다.
        if (!ReportOrphans())
        {
            return 1;
        }
        std::cout << "suite lists match\n";
        return 0;
    }

    if (!arguments.empty() && arguments[0] == "--suite")
    {
        if (arguments.size() != 2)
        {
            PrintUsage();
            return 1;
        }
        if (std::ranges::find(SuiteNames, arguments[1]) == SuiteNames.end())
        {
            std::cerr << "unknown suite: " << arguments[1] << '\n';
            PrintUsage();
            return 1;
        }
        if (!RunSuite(arguments[1]))
        {
            return 1;
        }
        std::cout << arguments[1] << " suite passed\n";
        return 0;
    }

    if (!arguments.empty())
    {
        PrintUsage();
        return 1;
    }

    bool passed = ReportOrphans();
    for (const std::string_view suite : SuiteNames)
    {
        passed &= RunSuite(suite);
    }
    if (!passed)
    {
        return 1;
    }
    std::cout << "GameEngineTests passed\n";
    return 0;
}
