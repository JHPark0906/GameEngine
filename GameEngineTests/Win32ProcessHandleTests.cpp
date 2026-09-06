#include "Win32ProcessHandleTests.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string_view>
#include <thread>
#include <windows.h>

#include "Platform/IProcessRunner.h"
#include "Platform/PlatformServices.h"
#include "TestSupport.h"

namespace
{
    bool RunOnce(const GameEngine::Platform::ProcessRequest& request, const bool shouldStart)
    {
        auto runner = GameEngine::Platform::PlatformServices::CreateProcessRunner();
        if (!TestSupport::Expect(runner && runner->Start(request) == shouldStart,
                "process startup reports the expected result"))
        {
            return false;
        }
        if (!shouldStart)
        {
            return true;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline)
        {
            runner->Poll([](std::string_view) {});
            if (const std::optional<int> code = runner->TakeExitCode())
            {
                return TestSupport::Expect(*code == 0, "the process completes successfully");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return TestSupport::Expect(false, "the process completes before the deadline");
    }
}

bool RunWin32ProcessHandleTests()
{
    wchar_t systemDirectory[MAX_PATH]{};
    if (!TestSupport::Expect(GetSystemDirectoryW(systemDirectory, MAX_PATH) > 0,
            "the Windows system directory is available"))
    {
        return false;
    }
    TestSupport::TemporaryDirectory temporary("ProcessHandleLifetime");
    GameEngine::Platform::ProcessRequest success;
    success.executable = std::filesystem::path(systemDirectory) / "cmd.exe";
    success.arguments = { "/c", "exit", "0" };
    GameEngine::Platform::ProcessRequest failure;
    failure.executable = temporary.GetPath() / "missing.exe";

    // Warm up CRT/logging before counting kernel handles retained by repeated runs.
    if (!RunOnce(success, true) || !RunOnce(failure, false))
    {
        return false;
    }
    DWORD before = 0;
    if (!TestSupport::Expect(GetProcessHandleCount(GetCurrentProcess(), &before) != FALSE,
            "the initial process handle count can be read"))
    {
        return false;
    }
    for (int iteration = 0; iteration < 16; ++iteration)
    {
        if (!RunOnce(success, true) || !RunOnce(failure, false))
        {
            return false;
        }
    }
    DWORD after = 0;
    return TestSupport::Expect(GetProcessHandleCount(GetCurrentProcess(), &after) != FALSE,
               "the final process handle count can be read") &&
        TestSupport::Expect(after <= before, "successful and failed starts retain no kernel handles");
}

static const TestSupport::Registration gWin32ProcessHandleTests{
    "ProjectBuilder", "process startup closes temporary handles", RunWin32ProcessHandleTests };
