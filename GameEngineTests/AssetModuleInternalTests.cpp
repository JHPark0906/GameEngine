#include "AssetModuleInternalTests.h"

#include <filesystem>
#include <iostream>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::ReadFile;

namespace
{
    /// <summary>모듈 안에서만 include해도 되는 헤더들이다.</summary>
    constexpr std::string_view InternalHeader = "AssetDatabaseInternal.h";

    /// <summary>그 헤더를 include해도 되는 자리다.</summary>
    constexpr std::string_view OwningModule = "/GameEngine/Assets/";

    [[nodiscard]] std::filesystem::path RepositoryRoot()
    {
        // 이 파일은 <repo>/GameEngineTests/AssetModuleInternalTests.cpp에 있다.
        return std::filesystem::path(__FILE__).parent_path().parent_path();
    }
}

bool RunAssetModuleInternalTests()
{
    const std::filesystem::path repository = RepositoryRoot();
    // 앞에 붙는 디렉터리가 무엇이든 잡아야 한다. 모듈 밖에서는 "Assets/AssetDatabaseInternal.h"
    // 처럼 경로를 붙여 include하므로, 이름만 보고 판단하면 정확히 그 경우를 놓친다.
    const std::string needle = std::string(InternalHeader) + "\"";

    std::error_code error;
    int includingFiles = 0;
    bool everyIncludeIsInside = true;
    bool theHeaderExists = false;
    for (std::filesystem::recursive_directory_iterator iterator(repository, error), end;
        iterator != end; iterator.increment(error))
    {
        if (error)
        {
            break;
        }
        const std::filesystem::path& path = iterator->path();
        const std::string generic = "/" + path.lexically_relative(repository).generic_string();
        // 빌드 산출물은 소스가 아니다. 거기 있는 사본은 이 규칙이 답할 것이 아니다.
        if (generic.find("/build/") != std::string::npos || generic.find("/x64") != std::string::npos || generic.find("/.git/") != std::string::npos)
        {
            iterator.disable_recursion_pending();
            continue;
        }
        if (!iterator->is_regular_file())
        {
            continue;
        }
        const std::filesystem::path extension = path.extension();
        if (extension != ".h" && extension != ".cpp")
        {
            continue;
        }
        if (path.filename() == InternalHeader)
        {
            theHeaderExists = true;
            continue;
        }
        // 줄 단위로 본다. 파일 어딘가에 그 이름이 적혀 있다는 것과 그 파일이 그것을
        // include한다는 것은 다른 말이고, 이 시험 자신이 바로 그 차이의 예다.
        bool includesIt = false;
        std::istringstream lines(ReadFile(path));
        std::string line;
        while (std::getline(lines, line))
        {
            const std::size_t firstWord = line.find_first_not_of(" 	");
            if (firstWord == std::string::npos || line.compare(firstWord, 8, "#include") != 0)
            {
                continue;
            }
            if (line.find(needle) != std::string::npos)
            {
                includesIt = true;
                break;
            }
        }
        if (!includesIt)
        {
            continue;
        }
        ++includingFiles;
        if (!std::string_view(generic).starts_with(OwningModule))
        {
            std::cerr << "FAILED: this file includes an asset module internal header from outside "
                      << "the module. file=" << generic << '\n';
            everyIncludeIsInside = false;
        }
    }

    return Expect(!error, "the source tree must be fully readable") &&
        Expect(theHeaderExists, "the asset module's internal header should be there") &&
        Expect(
            includingFiles > 0,
            "something should include it; otherwise this rule guards nothing") &&
        Expect(
            everyIncludeIsInside,
            "an asset module internal header should be included only inside that module");
}

static const TestSupport::Registration gAssetModuleInternalTests{
    "Layering", "asset module internal tests should pass", RunAssetModuleInternalTests };
