#include "EditorToolbarDeclarationTests.h"

#include <algorithm>
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
    /// <summary>툴바를 세우는 파일이다. 이 시험은 <repo>/GameEngineTests 안에 있다.</summary>
    [[nodiscard]] std::filesystem::path ToolbarSourcePath()
    {
        return std::filesystem::path(__FILE__).parent_path().parent_path() / "GameEditor" /
            "Source" / "Views" / "EditorToolbarView.cpp";
    }
}

bool RunEditorToolbarDeclarationTests()
{
    std::error_code error;
    const std::filesystem::path source = ToolbarSourcePath();
    if (!std::filesystem::is_regular_file(source, error) || error)
    {
        std::cout << "  toolbar declaration tests skipped: toolbar source not found at "
                  << source.string() << "\n";
        return true;
    }

    std::ifstream stream(source);
    std::string line;
    std::vector<std::string> declared;
    std::vector<std::string> arranged;
    bool insideRowButtons = false;

    const std::regex buttonPattern(R"raw(AddButton\(scene, \*mButtonGroup, "([^"]+)"\))raw");
    const std::regex memberPattern(R"raw(&(m[A-Za-z]+))raw");
    while (std::getline(stream, line))
    {
        std::smatch match;
        if (std::regex_search(line, match, buttonPattern))
        {
            declared.push_back(match[1].str());
        }

        // mRowButtons의 초기화 목록이다. 버튼을 만들어 놓고 이 목록에 넣지 않으면 그 버튼은
        // 자리를 받지 못한 채 첫 버튼 위에 겹쳐 남는다 — 화면에서는 "버튼 하나가 이상하게
        // 생겼다"로만 보이므로 원인을 짚기 어렵다.
        if (line.find("mRowButtons = {") != std::string::npos)
        {
            insideRowButtons = true;
        }
        if (insideRowButtons)
        {
            for (auto it = std::sregex_iterator(line.begin(), line.end(), memberPattern);
                 it != std::sregex_iterator(); ++it)
            {
                arranged.push_back((*it)[1].str());
            }
            if (line.find("};") != std::string::npos)
            {
                insideRowButtons = false;
            }
        }
    }

    bool passed = true;
    passed &= Expect(!declared.empty(), "the toolbar declares buttons");
    passed &= Expect(!arranged.empty(), "and hands a list of them to the layout");

    if (declared.size() != arranged.size())
    {
        std::cerr << "  the toolbar declares " << declared.size() << " button(s) but arranges "
                  << arranged.size() << ":\n";
        for (const std::string& name : declared)
        {
            std::cerr << "    declared: " << name << "\n";
        }
        for (const std::string& name : arranged)
        {
            std::cerr << "    arranged: " << name << "\n";
        }
    }
    passed &= Expect(
        declared.size() == arranged.size(),
        "every button the toolbar declares is one the layout is given a place for");

    // 같은 멤버를 두 번 적으면 개수는 맞으면서 한 버튼이 자리를 못 받는다. 개수만으로는 그것이
    // 걸리지 않으므로 중복도 함께 본다.
    std::vector<std::string> sorted = arranged;
    std::ranges::sort(sorted);
    const bool unique = std::ranges::adjacent_find(sorted) == sorted.end();
    passed &= Expect(unique, "and no button is listed twice at the expense of another");
    return passed;
}

static const TestSupport::Registration gEditorToolbarDeclarationTests{
    "EditorDocument", "editor toolbar declaration tests should pass", RunEditorToolbarDeclarationTests };
