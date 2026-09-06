#include "Win32WindowCloseTests.h"

#include <windows.h>

#include <exception>
#include <string>
#include <thread>

#include "Platform/Win32/Win32Window.h"
#include "TestSupport.h"

namespace
{
    using GameEngine::Platform::WindowDescription;
    using GameEngine::Platform::WindowMessageResult;
    using GameEngine::Platform::Win32::Win32Window;
    using TestSupport::Expect;

    struct DesktopScope final
    {
        HDESK handle = nullptr;

        ~DesktopScope()
        {
            if (handle)
            {
                static_cast<void>(CloseDesktop(handle));
            }
        }
    };

    bool DrainWithoutClose(Win32Window& window)
    {
        for (int index = 0; index < 256; ++index)
        {
            int exitCode = -1;
            const auto result = window.ProcessMessage(exitCode);
            if (result == WindowMessageResult::Idle)
            {
                return true;
            }
            if (!Expect(result == WindowMessageResult::Processed,
                "a window should not receive an extra close or quit notification"))
            {
                return false;
            }
        }
        return Expect(false, "the isolated window message queue should become idle");
    }

    bool InitializeWindow(Win32Window& window)
    {
        WindowDescription description;
        description.title = L"GameEngine close regression";
        description.clientWidth = 128;
        description.clientHeight = 96;
        if (!Expect(window.Initialize(description), "the native close fixture should create a window"))
        {
            return false;
        }
        static_cast<void>(ShowWindow(static_cast<HWND>(window.GetSurface().handle), SW_HIDE));
        return DrainWithoutClose(window);
    }

    bool ExpectNoQueuedQuit()
    {
        int quitCount = 0;
        MSG message{};
        while (PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE))
        {
            ++quitCount;
        }
        return Expect(quitCount == 0, "window teardown must not leave WM_QUIT in the thread queue");
    }

    bool CheckCloseOnWindowThread()
    {
        bool passed = true;
        HWND closedHandle = nullptr;
        {
            Win32Window window;
            if (!InitializeWindow(window))
            {
                return false;
            }
            closedHandle = static_cast<HWND>(window.GetSurface().handle);
            window.RequestClose();
            window.RequestClose();
            RECT client{};
            passed &= Expect(IsWindow(closedHandle) && GetClientRect(closedHandle, &client),
                "approved close must preserve the native surface for in-flight rendering");
            int exitCode = -1;
            passed &= Expect(window.ProcessMessage(exitCode) == WindowMessageResult::Quit && exitCode == 0,
                "approved close should report Quit with exit code zero on the next poll");
            window.RequestClose();
            passed &= DrainWithoutClose(window);
            passed &= Expect(IsWindow(closedHandle) != FALSE, "consuming Quit must not destroy the native surface");
        }
        passed &= Expect(!IsWindow(closedHandle), "the window owner should destroy its native surface");
        passed &= ExpectNoQueuedQuit();

        {
            Win32Window window;
            if (!InitializeWindow(window))
            {
                return false;
            }
            PostQuitMessage(37);
            window.RequestClose();
            int exitCode = -1;
            passed &= Expect(window.ProcessMessage(exitCode) == WindowMessageResult::Quit && exitCode == 37,
                "approved close should consume an already queued WM_QUIT and preserve its exit code");
            passed &= ExpectNoQueuedQuit();
            passed &= DrainWithoutClose(window);
        }
        passed &= ExpectNoQueuedQuit();

        // An unconsumed close request must not become another window's quit notification.
        {
            Win32Window window;
            if (!InitializeWindow(window))
            {
                return false;
            }
            closedHandle = static_cast<HWND>(window.GetSurface().handle);
            window.RequestClose();
        }
        passed &= Expect(!IsWindow(closedHandle), "teardown should destroy a window with unconsumed Quit");
        passed &= ExpectNoQueuedQuit();
        {
            Win32Window nextWindow;
            if (!InitializeWindow(nextWindow))
            {
                return false;
            }
            passed &= DrainWithoutClose(nextWindow);
        }
        passed &= ExpectNoQueuedQuit();

        {
            Win32Window window;
            if (!InitializeWindow(window))
            {
                return false;
            }
            const HWND handle = static_cast<HWND>(window.GetSurface().handle);
            if (!Expect(PostMessageW(handle, WM_CLOSE, 0, 0) != FALSE, "the fixture should post a user close request"))
            {
                return false;
            }
            bool closeRequested = false;
            for (int index = 0; index < 256 && !closeRequested; ++index)
            {
                int exitCode = -1;
                const auto result = window.ProcessMessage(exitCode);
                closeRequested = result == WindowMessageResult::CloseRequested;
                passed &= Expect(result != WindowMessageResult::Quit,
                    "WM_CLOSE should ask for approval before reporting Quit");
                if (result == WindowMessageResult::Idle)
                {
                    break;
                }
            }
            passed &= Expect(closeRequested && IsWindow(handle),
                "a user close request should keep the window alive while approval is pending");
            window.RequestClose();
            int exitCode = -1;
            passed &= Expect(window.ProcessMessage(exitCode) == WindowMessageResult::Quit && IsWindow(handle),
                "approving WM_CLOSE should report Quit without destroying the surface");
        }
        passed &= ExpectNoQueuedQuit();

        {
            Win32Window window;
            if (!InitializeWindow(window))
            {
                return false;
            }
            const HWND handle = static_cast<HWND>(window.GetSurface().handle);
            passed &= Expect(DestroyWindow(handle) != FALSE, "external native destruction should succeed");
            passed &= Expect(!IsWindow(handle) && window.GetSurface().handle == nullptr,
                "native destruction should clear the owner's surface handle");
            int exitCode = -1;
            passed &= Expect(window.ProcessMessage(exitCode) == WindowMessageResult::Quit && exitCode == 0,
                "external native destruction should still notify the application to quit");
            passed &= DrainWithoutClose(window);
        }
        passed &= ExpectNoQueuedQuit();

        {
            Win32Window window;
            if (!InitializeWindow(window))
            {
                return false;
            }
            PostQuitMessage(17);
            int exitCode = -1;
            passed &= Expect(window.ProcessMessage(exitCode) == WindowMessageResult::Quit && exitCode == 17,
                "an external WM_QUIT should retain its exit code");
            window.RequestClose();
            passed &= DrainWithoutClose(window);
        }
        passed &= ExpectNoQueuedQuit();
        return passed;
    }
}

bool RunWin32WindowCloseTests()
{
    // Keep the native window and its message queue off the test runner's UI thread and desktop.
    const std::wstring name = L"GameEngineWindowCloseTests-" + std::to_wstring(GetCurrentProcessId());
    DesktopScope desktop{ CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr) };
    if (!TestSupport::Expect(desktop.handle != nullptr, "the native close fixture should create an inactive desktop"))
    {
        return false;
    }
    bool passed = false;
    std::exception_ptr error;
    std::thread worker([&]
    {
        try
        {
            if (TestSupport::Expect(SetThreadDesktop(desktop.handle) != FALSE,
                "the native close fixture should isolate its window on the inactive desktop"))
            {
                passed = CheckCloseOnWindowThread();
            }
        }
        catch (...)
        {
            error = std::current_exception();
        }
    });
    worker.join();
    if (error)
    {
        std::rethrow_exception(error);
    }
    return passed;
}

static const TestSupport::Registration gWin32WindowCloseTests{
    "PlayerStartup", "native close should preserve the surface until owner teardown", RunWin32WindowCloseTests };
