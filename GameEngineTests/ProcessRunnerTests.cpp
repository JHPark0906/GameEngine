#include "ProcessRunnerTests.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "Platform/IProcessRunner.h"
#include "Platform/PlatformServices.h"

// 자식 콘솔이 어느 코드페이지로 쓰는지 확인하는 데에만 쓴다. 표준 헤더 뒤에 두어 이 헤더의
// 매크로가 앞의 것들에 닿지 않게 한다.
#include <windows.h>
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>시험용 스크립트들이 놓이는 디렉터리다.</summary>
    [[nodiscard]] std::filesystem::path ScriptDirectory()
    {
        static TestSupport::TemporaryDirectory directory("ProcessRunnerScripts");
        return directory.GetPath();
    }

    /// <summary>
    /// 시킬 일을 배치 파일에 적고, 그것을 돌릴 요청을 만든다.
    ///
    /// 명령을 <c>cmd /c "..."</c>로 바로 넘기지 않는 이유가 있다. <c>cmd.exe</c>의 명령줄 해석은
    /// 다른 프로그램과 달라서, 따옴표 안에 <c>&amp;</c>나 <c>&gt;</c>가 있으면 따옴표를 벗기지 않고
    /// 통째로 프로그램 이름으로 읽는다 — 그러면 아무것도 실행되지 않은 채 「구문이 잘못되었습니다」만
    /// 돌아온다. 스크립트로 옮기면 인자가 파일 이름 하나뿐이라 그 규칙에 걸릴 자리가 없다.
    /// </summary>
    /// <param name="name">스크립트 이름이다. 공백이 없어야 한다.</param>
    /// <param name="body">배치 파일에 그대로 들어갈 바이트다.</param>
    [[nodiscard]] GameEngine::Platform::ProcessRequest ScriptCommand(
        const std::string& name, const std::string& body)
    {
        const std::filesystem::path directory = ScriptDirectory();
        std::filesystem::create_directories(directory);
        const std::string fileName = name + ".cmd";
        {
            std::ofstream file(directory / fileName, std::ios::binary | std::ios::trunc);
            // @echo off가 없으면 cmd가 시킨 명령까지 출력에 섞어, 줄 순서 시험이 세는 줄이 달라진다.
            file << "@echo off\r\n" << body;
        }

        GameEngine::Platform::ProcessRequest request;
        // 실행 파일을 슬래시가 든 경로로 준다. std::filesystem이 만드는 경로가 대개 그 모양이고,
        // cmd.exe는 그런 경로로 불리면 시작하지 못한다 — 러너가 구분자를 고쳐 준다는 약속을
        // 여기서 함께 지킨다.
        request.executable = L"C:/Windows/System32/cmd.exe";
        request.workingDirectory = directory;
        // 이름만 주지 않고 온전한 경로를 준다. 현재 디렉터리에서 프로그램을 찾는 것은 환경
        // 변수 하나로 꺼져 있을 수 있고(<c>NoDefaultCurrentDirectoryInExePath</c>), 그러면
        // 스크립트가 바로 옆에 있어도 「경로를 찾을 수 없습니다」가 된다.
        request.arguments = { "/c", (directory / fileName).string() };
        return request;
    }

    /// <summary>
    /// 끝날 때까지 프레임처럼 돌린다. 진짜 에디터가 하는 것과 같은 모양이다: 막지 않고,
    /// 프레임마다 모인 줄만 가져간다.
    /// </summary>
    /// <returns>종료 코드다. 제한 시간 안에 끝나지 않으면 비어 있다.</returns>
    [[nodiscard]] std::optional<int> PumpUntilDone(
        GameEngine::Platform::IProcessRunner& runner, std::vector<std::string>& lines,
        const std::chrono::seconds limit = std::chrono::seconds(30))
    {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        for (;;)
        {
            runner.Poll([&lines](const std::string_view line)
            {
                lines.emplace_back(line);
            });
            if (const std::optional<int> code = runner.TakeExitCode())
            {
                // 끝난 뒤에도 큐에 남은 줄이 있을 수 있다.
                runner.Poll([&lines](const std::string_view line)
                {
                    lines.emplace_back(line);
                });
                return code;
            }
            if (std::chrono::steady_clock::now() > deadline)
            {
                return std::nullopt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    /// <summary>이름이 주어진 프로세스가 지금 돌고 있는지다.</summary>
    [[nodiscard]] bool IsProcessRunning(const std::string& imageName)
    {
        // 온전한 경로로 부른다. PATH 앞쪽에 다른 find가 있으면 — Git Bash의 유닉스 find이
        // 그렇다 — 이 검사는 프로세스가 돌고 있어도 없다고 답한다.
        const std::string command =
            "C:\\Windows\\System32\\tasklist.exe /FI \"IMAGENAME eq " + imageName +
            "\" | C:\\Windows\\System32\\find.exe /I \"" + imageName + "\" >NUL 2>&1";
        return std::system(command.c_str()) == 0;
    }

    /// <summary>
    /// 그 이미지의 실행 상태가 원하는 값이 될 때까지 기다리고 결과를 돌려준다.
    /// 프로세스의 시작과 종료 시간은 기계 부하에 따라 달라지므로 고정 대기 후 추측하지 않는다.
    /// 기한은 기다림의 상한이며, 실제 실행 상태를 확인한 결과가 성공 여부를 결정한다.
    /// </summary>
    [[nodiscard]] bool WaitUntilProcessIs(
        const std::string& imageName, const bool wanted,
        const std::chrono::seconds limit = std::chrono::seconds(20))
    {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        for (;;)
        {
            const bool running = IsProcessRunning(imageName);
            if (running == wanted || std::chrono::steady_clock::now() >= deadline)
            {
                return running;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

bool RunProcessRunnerTests()
{
    using GameEngine::Platform::PlatformServices;
    bool passed = true;

    // ⑴ 줄이 순서대로 온다. 빌드 출력은 순서가 곧 뜻이라 — 어느 파일의 오류인지가 앞줄에 있다 —
    // 섞이면 콘솔에서 읽을 수 없게 된다.
    {
        const auto runner = PlatformServices::CreateProcessRunner();
        std::vector<std::string> lines;
        passed &= Expect(
            runner->Start(ScriptCommand(
                "ThreeLines", "echo first\r\necho second\r\necho third\r\n")),
            "a process starts");
        const std::optional<int> code = PumpUntilDone(*runner, lines);
        passed &= Expect(code.has_value() && *code == 0, "and its exit code arrives");
        const bool inOrder = lines.size() == 3 && lines[0] == "first" &&
            lines[1] == "second" && lines[2] == "third";
        if (!inOrder)
        {
            std::cerr << "  read back " << lines.size() << " line(s)\n";
            for (const std::string& line : lines)
            {
                std::cerr << "  | " << line << "\n";
            }
        }
        passed &= Expect(inOrder, "with every line, in the order the program wrote them");
    }

    // ⑵ 실패한 종료 코드도 그대로 온다. 빌드가 실패했다는 것을 이것으로 안다.
    {
        const auto runner = PlatformServices::CreateProcessRunner();
        std::vector<std::string> lines;
        passed &= Expect(
            runner->Start(ScriptCommand("ExitThree", "exit 3\r\n")),
            "a failing process starts");
        const std::optional<int> code = PumpUntilDone(*runner, lines);
        passed &= Expect(
            code.has_value() && *code == 3, "and its non-zero exit code arrives unchanged");
    }

    // ⑶ 종료 코드는 사건이라 한 번만 온다. 두 번 오면 성공 메시지가 프레임마다 다시 나온다.
    {
        const auto runner = PlatformServices::CreateProcessRunner();
        std::vector<std::string> lines;
        static_cast<void>(runner->Start(ScriptCommand("Done", "echo done\r\n")));
        passed &= Expect(
            PumpUntilDone(*runner, lines).has_value(), "the exit code arrives once");
        passed &= Expect(
            !runner->TakeExitCode().has_value(), "and is not delivered a second time");
    }

    // ⑷ 도는 중에 또 시작하지 않는다. 한 러너의 출력이 두 실행의 것으로 섞이면 어느 줄이
    // 무엇의 것인지 알 수 없다.
    {
        const auto runner = PlatformServices::CreateProcessRunner();
        passed &= Expect(
            runner->Start(ScriptCommand("Slow", "ping -n 3 127.0.0.1 >NUL\r\n")),
            "a slow process starts");
        passed &= Expect(
            !runner->Start(ScriptCommand("Second", "echo second\r\n")),
            "a second start is refused");
        runner->RequestCancel();
        std::vector<std::string> lines;
        static_cast<void>(PumpUntilDone(*runner, lines, std::chrono::seconds(10)));
    }

    // ⑸ 취소는 손자까지 끝낸다. cmake는 msbuild를, msbuild는 컴파일러를 낳으므로 직접 자식만
    // 죽이면 남은 도구가 빌드 트리에 락을 쥔 채 다음 빌드를 이유 없이 실패시킨다.
    {
        const auto runner = PlatformServices::CreateProcessRunner();
        const std::string grandchildName =
            "GameEnginePing-" + std::to_string(GetCurrentProcessId()) + ".exe";
        const std::filesystem::path grandchildPath = ScriptDirectory() / grandchildName;
        std::error_code copyError;
        std::filesystem::copy_file(
            "C:/Windows/System32/ping.exe", grandchildPath,
            std::filesystem::copy_options::overwrite_existing, copyError);
        if (!Expect(!copyError, "the grandchild has a process-specific executable name"))
        {
            return false;
        }
        // cmd가 자식이고 그 안에서 도는 ping이 손자다.
        passed &= Expect(
            runner->Start(ScriptCommand("Grandchild",
                "\"" + grandchildPath.string() + "\" -n 300 127.0.0.1 >NUL\r\n")),
            "a process that spawns a grandchild starts");
        // 손자가 뜬 것을 확인하고 나서 취소한다. 뜨기 전에 취소하면 이 검사는 아무것도
        // 증명하지 못한다 — 죽일 것이 없었을 뿐이다.
        const bool grandchildWasRunning = WaitUntilProcessIs(grandchildName, true);
        runner->RequestCancel();
        std::vector<std::string> lines;
        passed &= Expect(PumpUntilDone(*runner, lines, std::chrono::seconds(10)).has_value(),
            "cancelling the job closes the grandchild's inherited output pipe");
        // 부모가 끝났다고 손자가 그 순간 사라지는 것은 아니다. 사라진 것을 확인한다.
        const bool grandchildStillRunning = WaitUntilProcessIs(grandchildName, false);

        if (!grandchildWasRunning)
        {
            std::cerr << "  the grandchild never appeared; this check proved nothing\n";
        }
        passed &= Expect(
            grandchildWasRunning, "the grandchild should be running before the cancel");
        passed &= Expect(
            !grandchildStillRunning, "and cancelling the parent takes the grandchild with it");
    }

    // CP949 출력이 UTF-8로 변환되는지 확인한다. 현재 코드 페이지가 CP949가 아니면 건너뛴 사실을 기록한다.
    if (GetOEMCP() != 949)
    {
        std::cout << "  the console encoding check was skipped: this machine writes console text"
                     " in code page " << GetOEMCP() << ", not 949\n";
    }
    else
    {
        const auto runner = PlatformServices::CreateProcessRunner();
        std::vector<std::string> lines;
        // 「가나다」를 CP949 바이트로 직접 적는다. 파일이 UTF-8이라 소스에 그대로 쓸 수는 없고,
        // chcp로 코드페이지를 정해 두어야 이 시험이 기계의 기본값에 기대지 않는다.
        const std::string koreanInCp949 = "\xB0\xA1\xB3\xAA\xB4\xD9";
        passed &= Expect(
            runner->Start(ScriptCommand(
                "Korean", "chcp 949 >NUL\r\necho " + koreanInCp949 + "\r\n")),
            "a process that writes Korean starts");
        static_cast<void>(PumpUntilDone(*runner, lines));
        const bool readBack =
            !lines.empty() && lines.back() == std::string("\uAC00\uB098\uB2E4");
        if (!readBack && !lines.empty())
        {
            std::cerr << "  read back: " << lines.back() << "\n";
        }
        passed &= Expect(readBack, "and its text arrives as UTF-8 rather than mangled bytes");
    }
    return passed;
}

static const TestSupport::Registration gProcessRunnerTests{
    "ProjectBuilder", "process runner tests should pass", RunProcessRunnerTests };
