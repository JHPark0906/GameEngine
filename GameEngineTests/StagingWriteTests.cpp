#include "StagingWriteTests.h"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::ReadFile;

namespace
{
    [[nodiscard]] std::filesystem::path RepositoryRoot()
    {
        // 이 파일은 <repo>/GameEngineTests/StagingWriteTests.cpp에 있다.
        return std::filesystem::path(__FILE__).parent_path().parent_path();
    }

    /// <summary>
    /// 주석을 뺀 스크립트다. 주석은 규칙을 설명하면서 그 규칙이 금지하는 이름을 그대로 적기
    /// 마련이라 — 이 스크립트가 무엇을 대신하는지 말하는 문장이 그렇다 — 주석까지 훑으면 설명이
    /// 위반으로 읽힌다.
    /// </summary>
    [[nodiscard]] std::string WithoutComments(const std::string& script)
    {
        std::istringstream lines(script);
        std::string stripped;
        std::string line;
        while (std::getline(lines, line))
        {
            const std::size_t firstWord = line.find_first_not_of(" \t\r");
            if (firstWord == std::string::npos || line[firstWord] != '#')
            {
                stripped += line;
                stripped += ' ';
            }
        }
        return stripped;
    }

    /// <summary>`add_custom_command(` 하나가 닫히는 괄호까지의 본문들이다.</summary>
    [[nodiscard]] std::vector<std::string> CustomCommandsIn(const std::string& script)
    {
        constexpr std::string_view Opening = "add_custom_command(";
        std::vector<std::string> commands;
        for (std::size_t at = script.find(Opening); at != std::string::npos;
            at = script.find(Opening, at + 1))
        {
            std::size_t depth = 0;
            std::size_t index = at + Opening.size() - 1;
            for (; index < script.size(); ++index)
            {
                if (script[index] == '(')
                {
                    ++depth;
                }
                else if (script[index] == ')' && --depth == 0)
                {
                    break;
                }
            }
            commands.push_back(script.substr(at, index - at));
        }
        return commands;
    }
}

bool RunStagingWriteTests()
{
    const std::filesystem::path cmakeDirectory = RepositoryRoot() / "cmake";
    const std::string stageScript = ReadFile(cmakeDirectory / "StageFile.cmake");

    // 스테이징 스크립트는 이름 바꾸기로 자리를 차지해야 한다. 복사로 돌아가면 이 시험이 지키는
    // 것이 이름만 남는다.
    const bool stagingRenamesIntoPlace = !stageScript.empty() &&
        stageScript.find("file(RENAME") != std::string::npos &&
        stageScript.find("file(COPY_FILE") != std::string::npos;

    // 실행 파일 옆에 놓는 모든 명령이 그 스크립트를 거쳐야 한다.
    const std::string projectScript =
        WithoutComments(ReadFile(cmakeDirectory / "GameEngineProject.cmake"));
    bool everyStagingGoesThroughIt = !projectScript.empty();
    int stagingCommands = 0;
    for (const std::string& command : CustomCommandsIn(projectScript))
    {
        // 스테이징 명령은 목적지를 staged 계열 변수로 받는다. 그 이름이 이 파일에서 「실행
        // 파일 옆자리」를 뜻한다.
        if (command.find("${staged") == std::string::npos)
        {
            continue;
        }
        ++stagingCommands;
        if (command.find("StageFile.cmake") == std::string::npos)
        {
            std::cerr << "FAILED: this staging command does not place the file by rename.\n"
                      << command << '\n';
            everyStagingGoesThroughIt = false;
        }
    }

    // 목적지에 바로 쓰는 복사는 어디에도 남아 있으면 안 된다. 한 자리만 남아도 그 파일은
    // 다시 찢어지고, 왜 그 파일만인지는 아무 데도 적혀 있지 않다.
    bool nothingCopiesIntoPlace = true;
    for (const std::filesystem::directory_entry& entry :
        std::filesystem::directory_iterator(cmakeDirectory))
    {
        if (entry.path().extension() != ".cmake")
        {
            continue;
        }
        if (WithoutComments(ReadFile(entry.path())).find("copy_if_different") != std::string::npos)
        {
            std::cerr << "FAILED: this script still copies into the destination in place. script="
                      << entry.path().filename().string() << '\n';
            nothingCopiesIntoPlace = false;
        }
    }

    return Expect(
            stagingRenamesIntoPlace,
            "the staging script should write a temporary file and rename it into place") &&
        Expect(stagingCommands > 0, "there should be staging commands to check") &&
        Expect(
            everyStagingGoesThroughIt,
            "every command that places a file beside an executable should stage it by rename") &&
        Expect(
            nothingCopiesIntoPlace,
            "no build script should copy straight into a file something else may be reading");
}

static const TestSupport::Registration gStagingWriteTests{
    "Core", "staging write tests should pass", RunStagingWriteTests };
