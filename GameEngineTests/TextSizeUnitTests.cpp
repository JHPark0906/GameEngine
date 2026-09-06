#include "TextSizeUnitTests.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>
    /// 글자 크기에 배율을 곱해 넘기는 꼴들이다.
    ///
    /// 두 가지를 함께 잡는다. <b>선언</b>에 곱하는 것(<c>SetFontSize(X * scale)</c>)과,
    /// <b>재는 호출</b>에 곱해 넘기는 것(<c>MeasureWidth(..., X * scale)</c>)이다. 앞의 것만
    /// 막으면 뒤의 것이 남고, 남은 쪽은 같은 병을 같은 크기로 앓는다 — 잰 폭이 배율만큼
    /// 커지므로 그 폭으로 잡은 자리도 그만큼 어긋난다.
    /// </summary>
    const std::array<std::pair<std::string_view, std::regex>, 2> ForbiddenForms{
        std::pair{ std::string_view("a font size declared with the scale multiplied in"),
            std::regex{ R"(SetFontSize\s*\([^)]*\*\s*[A-Za-z_]*[Ss]cale)" } },
        std::pair{ std::string_view("a font size measured with the scale multiplied in"),
            std::regex{ R"(Measure[A-Za-z]*\s*\([^)]*FontSize\s*\*\s*[A-Za-z_]*[Ss]cale)" } },
    };

    constexpr std::array<std::string_view, 2> ScannedDirectories{ "GameEngine", "GameEditor" };
}

bool RunTextSizeUnitTests()
{
    namespace fs = std::filesystem;

    // 이 파일은 <repo>/GameEngineTests/TextSizeUnitTests.cpp에 있다.
    const fs::path repository = fs::path(__FILE__).parent_path().parent_path();
    std::error_code error;
    if (!fs::is_directory(repository, error))
    {
        std::cout << "  text size unit test skipped: sources not found\n";
        return true;
    }

    std::vector<std::string> found;
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
            std::ifstream stream(path);
            std::string line;
            unsigned int lineNumber = 0;
            while (std::getline(stream, line))
            {
                ++lineNumber;
                const std::size_t firstCharacter = line.find_first_not_of(" \t");
                if (firstCharacter != std::string::npos &&
                    line.compare(firstCharacter, 2, "//") == 0)
                {
                    // 규칙을 설명하는 주석은 그 꼴을 적을 수 있어야 한다.
                    continue;
                }
                for (const auto& [what, pattern] : ForbiddenForms)
                {
                    if (std::regex_search(line, pattern))
                    {
                        found.push_back(
                            fs::relative(path, repository, error).generic_string() + ":" +
                            std::to_string(lineNumber) + " has " + std::string(what));
                    }
                }
            }
        }
    }

    for (const std::string& site : found)
    {
        std::cerr << "  text size unit: " << site << "\n";
    }
    return Expect(
        found.empty(),
        "a font size is declared and measured in logical units; the scale is applied once, "
        "where the text is drawn");
}

static const TestSupport::Registration gTextSizeUnitTests{
    "Core", "text size unit tests should pass", RunTextSizeUnitTests };
