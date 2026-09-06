#include "pch.h"
#include "Win32Window.h"

#include "Win32ChoiceDialog.h"
#include "../../Diagnostics/Debug.h"
#include "Win32IconResource.h"

#include <dwmapi.h>
#include <vector>

#pragma comment(lib, "dwmapi.lib")

namespace GameEngine::Platform::Win32
{

Win32Window::Win32Window()
    // The module handle identifies this executable and is obtained within the platform implementation.
    : mInstanceHandle(GetModuleHandleW(nullptr))
{
}

Win32Window::~Win32Window()
{
    mDestroying = true;
    if (mWindowHandle && IsWindow(mWindowHandle))
    {
        DestroyWindow(mWindowHandle);
    }

    if (mInstanceHandle)
    {
        UnregisterClassW(WindowClassName, mInstanceHandle);
    }
}

bool Win32Window::Initialize(const WindowDescription& description)
{
    if (!RegisterWindowClass())
    {
        return false;
    }

    mWindowChrome = description.chrome;
    mWindowTheme = description.theme;
    // 프로젝트가 적는 창 크기는 96 DPI 기준의 논리 픽셀이다. 이 프로세스는 DPI를 인지하므로
    // 시스템이 대신 키워 주지 않고, 조밀한 화면에서 같은 물리 크기의 창이 되도록 여기서 곱한다.
    const UINT dpi = GetDpiForSystem();
    const int clientWidth = MulDiv(description.clientWidth, static_cast<int>(dpi), 96);
    const int clientHeight = MulDiv(description.clientHeight, static_cast<int>(dpi), 96);
    // 커스텀 모드도 시스템의 접근성, 스냅 레이아웃 및 창 제어 버튼을 유지한다.
    // 앱은 그 아래 클라이언트 영역에 명령 모음만 직접 그린다.
    const DWORD windowStyle = WS_OVERLAPPEDWINDOW;
    RECT windowRectangle = { 0, 0, clientWidth, clientHeight };
    if (!AdjustWindowRectExForDpi(&windowRectangle, windowStyle, FALSE, 0, dpi))
    {
        Diagnostics::Debug::LogError("AdjustWindowRect failed.");
        return false;
    }

    mWindowHandle = CreateWindowW(
        WindowClassName,
        description.title.c_str(),
        windowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRectangle.right - windowRectangle.left,
        windowRectangle.bottom - windowRectangle.top,
        nullptr,
        nullptr,
        mInstanceHandle,
        this);

    if (!mWindowHandle)
    {
        Diagnostics::Debug::LogError("CreateWindowW failed.");
        return false;
    }

    ApplyWindowTheme();

    ShowWindow(mWindowHandle, SW_SHOW);
    UpdateWindow(mWindowHandle);
    return true;
}

NativeSurface Win32Window::GetSurface() const
{
    return NativeSurface::FromWin32Window(mWindowHandle);
}

namespace
{
    /// <summary>지금 붙어 있는 모니터들의 사각형이다.</summary>
    [[nodiscard]] std::vector<ScreenRectangle> CollectMonitorRectangles()
    {
        std::vector<ScreenRectangle> monitors;
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](const HMONITOR monitor, HDC, LPRECT, const LPARAM parameter) -> BOOL
            {
                MONITORINFO information{};
                information.cbSize = sizeof(information);
                if (GetMonitorInfoW(monitor, &information))
                {
                    reinterpret_cast<std::vector<ScreenRectangle>*>(parameter)->push_back(
                        { information.rcMonitor.left, information.rcMonitor.top,
                            information.rcMonitor.right, information.rcMonitor.bottom });
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&monitors));
        return monitors;
    }
}

bool Win32Window::SetFullscreen(const bool fullscreen)
{
    if (!mWindowHandle || !IsWindow(mWindowHandle) || fullscreen == mFullscreen)
    {
        return mWindowHandle && IsWindow(mWindowHandle);
    }

    if (fullscreen)
    {
        RECT windowBounds{};
        if (!GetWindowRect(mWindowHandle, &windowBounds))
        {
            Diagnostics::Debug::LogError("The window's bounds could not be read.");
            return false;
        }
        mPlacementMemory.Remember(
            { windowBounds.left, windowBounds.top, windowBounds.right, windowBounds.bottom });
        mWindowedStyle =
            static_cast<DWORD>(GetWindowLongPtrW(mWindowHandle, GWL_STYLE));

        MONITORINFO information{};
        information.cbSize = sizeof(information);
        if (!GetMonitorInfoW(
                MonitorFromWindow(mWindowHandle, MONITOR_DEFAULTTONEAREST), &information))
        {
            Diagnostics::Debug::LogError("The window's monitor could not be read.");
            return false;
        }

        SetWindowLongPtrW(mWindowHandle, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        const RECT& monitorBounds = information.rcMonitor;
        // SWP_FRAMECHANGED가 없으면 스타일만 바뀌고 비클라이언트 영역이 다시 계산되지 않아,
        // 사라진 테두리가 자리를 그대로 차지한다.
        SetWindowPos(
            mWindowHandle, HWND_TOP, monitorBounds.left, monitorBounds.top,
            monitorBounds.right - monitorBounds.left, monitorBounds.bottom - monitorBounds.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        mFullscreen = true;
        return true;
    }

    const std::vector<ScreenRectangle> monitors = CollectMonitorRectangles();
    RECT workArea{ 0, 0, 1280, 720 };
    if (!monitors.empty())
    {
        // 기억한 자리를 쓸 수 없을 때 갈 곳이다: 지금 창이 있는 모니터의 왼쪽 위에 기본 크기로.
        const ScreenRectangle& first = monitors.front();
        workArea = { first.left, first.top, first.left + 1280, first.top + 720 };
    }
    const ScreenRectangle restored = mPlacementMemory.ResolveRestore(
        monitors, { workArea.left, workArea.top, workArea.right, workArea.bottom });

    SetWindowLongPtrW(
        mWindowHandle, GWL_STYLE,
        mWindowedStyle != 0 ? mWindowedStyle : DWORD{ WS_OVERLAPPEDWINDOW | WS_VISIBLE });
    SetWindowPos(
        mWindowHandle, HWND_NOTOPMOST, restored.left, restored.top, restored.GetWidth(),
        restored.GetHeight(), SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    ShowWindow(mWindowHandle, SW_SHOW);
    mPlacementMemory.Forget();
    mFullscreen = false;
    return true;
}

bool Win32Window::SetTheme(const NativeSurface& surface, const WindowTheme windowTheme) const
{
    if (surface.kind != NativeSurfaceKind::Win32)
    {
        return false;
    }
    const HWND windowHandle = static_cast<HWND>(surface.handle);
    if (!windowHandle || !IsWindow(windowHandle))
    {
        return false;
    }

    auto* owner = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(windowHandle, GWLP_USERDATA));
    if (!owner || owner->mWindowHandle != windowHandle)
    {
        return false;
    }

    owner->mWindowTheme = windowTheme;
    owner->ApplyWindowTheme();
    RedrawWindow(windowHandle, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    return true;
}

void Win32Window::ApplyWindowTheme()
{
    if (!mWindowHandle || !IsWindow(mWindowHandle) || mWindowTheme == WindowTheme::System)
    {
        return;
    }

    const BOOL useDarkMode = mWindowTheme == WindowTheme::Dark ? TRUE : FALSE;
    const COLORREF borderColor = mWindowTheme == WindowTheme::Dark
        ? RGB(16, 16, 16) : RGB(255, 255, 255);
    static_cast<void>(DwmSetWindowAttribute(
        mWindowHandle,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &useDarkMode,
        sizeof(useDarkMode)));
    static_cast<void>(DwmSetWindowAttribute(
        mWindowHandle,
        DWMWA_BORDER_COLOR,
        &borderColor,
        sizeof(borderColor)));
}

void Win32Window::RequestClose()
{
    // The owner keeps the surface alive until the render thread and graphics device stop.
    if (mWindowHandle && !mDestroying && mQuitState == QuitState::None)
    {
        mQuitState = QuitState::Pending;
    }
}

WindowMessageResult Win32Window::ProcessMessage(int& exitCode) const
{
    if (mQuitState == QuitState::Pending)
    {
        MSG quitMessage{};
        const bool hasNativeQuit = PeekMessageW(&quitMessage, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != FALSE;
        mQuitState = QuitState::Reported;
        exitCode = hasNativeQuit ? static_cast<int>(quitMessage.wParam) : 0;
        return WindowMessageResult::Quit;
    }

    MSG message = {};
    if (!PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        return WindowMessageResult::Idle;
    }

    if (message.message == WM_QUIT)
    {
        mQuitState = QuitState::Reported;
        exitCode = static_cast<int>(message.wParam);
        return WindowMessageResult::Quit;
    }

    TranslateMessage(&message);
    DispatchMessageW(&message);

    // 창 프로시저가 WM_CLOSE를 삼키고 표시만 남겼는지 본다. 디스패치 뒤라야 방금 꺼낸 메시지가
    // 버려지지 않는다. 닫을지는 프레임 밖에서 — 무엇이 저장되지 않았는지 아는 쪽이 — 정한다.
    if (mCloseRequested)
    {
        mCloseRequested = false;
        // 닫기 요청이 응용에게 갔다. 닫기를 그만두더라도 그 뒤의 물음은 정상으로 서야 한다.
        ClearChoiceDialogCancel();
        return WindowMessageResult::CloseRequested;
    }

    return WindowMessageResult::Processed;
}

bool Win32Window::RegisterWindowClass() const
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = mInstanceHandle;
    // 애플리케이션이 자기 아이콘을 실었으면 그것을 쓰고, 아니면 윈도우 기본 아이콘을 쓴다.
    // 엔진은 아이콘을 갖지 않는다: 얼굴은 애플리케이션의 것이고, 아이콘 없는 애플리케이션도
    // 창을 띄울 수 있어야 한다.
    //
    // 큰 것과 작은 것을 함께 준다. 큰 것만 주면 작업 표시줄과 Alt+Tab이 그것을 줄여 쓰는데,
    // .ico가 16픽셀 그림을 따로 담고 있어도 그 그림이 쓰이지 않는다.
    const HICON applicationIcon = LoadIconW(
        mInstanceHandle, MAKEINTRESOURCEW(GAMEENGINE_APPLICATION_ICON));
    windowClass.hIcon = applicationIcon ? applicationIcon : LoadIcon(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    windowClass.lpszClassName = WindowClassName;

    if (RegisterClassExW(&windowClass))
    {
        return true;
    }

    if (GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
    {
        return true;
    }

    Diagnostics::Debug::LogError("RegisterClassExW failed.");
    return false;
}

LRESULT Win32Window::WindowProcedure(
    const HWND windowHandle,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* owner = static_cast<Win32Window*>(create->lpCreateParams);
        SetWindowLongPtrW(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
        owner->mWindowHandle = windowHandle;
    }

    auto* owner = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(windowHandle, GWLP_USERDATA));
    if (owner)
    {
        const LRESULT result = owner->HandleMessage(message, wParam, lParam);
        if (message == WM_NCDESTROY)
        {
            SetWindowLongPtrW(windowHandle, GWLP_USERDATA, 0);
            owner->mWindowHandle = nullptr;
        }
        return result;
    }

    return DefWindowProcW(windowHandle, message, wParam, lParam);
}

LRESULT Win32Window::HandleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam)
{
    // Input is recorded before anything else looks at the message. A message the input consumes
    // entirely is one nothing else needs; everything else falls through to the handling below and
    // then to the default procedure, so recording a key never changes what the window does with it.
    if (mInput.HandleMessage(mWindowHandle, message, wParam, lParam))
    {
        return 0;
    }

    // Alt+Enter는 이 창이 처리한다. DXGI의 전체화면 메시지 감시는 present와 메시지 펌프가
    // 같은 스레드일 것을 전제하므로 별도 렌더 스레드를 쓰는 이 엔진에서는 꺼 둔다.
    // 토글 뒤 메시지를 삼켜 기본 처리가 시스템 메뉴를 열지 않게 한다.
    if (message == WM_SYSKEYDOWN && wParam == VK_RETURN)
    {
        static_cast<void>(SetFullscreen(!mFullscreen));
        return 0;
    }

    // 닫기 요청은 삼키고 표시만 남긴다. 여기서 닫으면 저장되지 않은 작업을 물어볼 자리가
    // 사라진다 — 묻는 것은 이 프로시저 밖의 일이다.
    if (message == WM_CLOSE)
    {
        mCloseRequested = true;
        // 물음이 떠 있으면 그것부터 취소로 끝낸다. 시작하며 뜨는 물음은 메시지 루프가 돌기
        // 전에 서므로, 여기서 알려 주지 않으면 사람에게는 창이 굳은 것으로 보인다.
        RequestChoiceDialogCancel();
        return 0;
    }

    if (message == WM_DESTROY)
    {
        RequestClose();
        return 0;
    }

    // 다른 배율의 모니터로 옮겨 가면 시스템이 같은 논리 크기가 되는 사각형을 제안한다. 그대로
    // 따르면 창이 물리 크기를 지키는 대신 사람에게 같은 크기로 보이고, 콘텐츠 배율은 UI가
    // 프레임마다 GetContentScale로 따라간다.
    if (message == WM_DPICHANGED)
    {
        const RECT* const suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested)
        {
            SetWindowPos(
                mWindowHandle, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }

    if (message == WM_ERASEBKGND && mWindowTheme != WindowTheme::System)
    {
        RECT clientBounds{};
        GetClientRect(mWindowHandle, &clientBounds);
        const HBRUSH background = CreateSolidBrush(
            mWindowTheme == WindowTheme::Dark ? RGB(0, 0, 0) : RGB(255, 255, 255));
        FillRect(reinterpret_cast<HDC>(wParam), &clientBounds, background);
        DeleteObject(background);
        return 1;
    }

    if (mWindowChrome == WindowChrome::Custom && message == WM_NCHITTEST)
    {
        const LRESULT defaultHit = DefWindowProcW(mWindowHandle, message, wParam, lParam);
        if (defaultHit != HTCLIENT)
        {
            return defaultHit;
        }

        POINT point{
            static_cast<LONG>(static_cast<short>(LOWORD(lParam))),
            static_cast<LONG>(static_cast<short>(HIWORD(lParam))) };
        ScreenToClient(mWindowHandle, &point);
        RECT bounds{};
        GetClientRect(mWindowHandle, &bounds);
        const UINT dpi = GetDpiForWindow(mWindowHandle);
        const int resizeBorder = static_cast<int>(GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)) +
            static_cast<int>(GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi));
        const bool left = point.x < resizeBorder;
        const bool right = point.x >= bounds.right - resizeBorder;
        const bool top = point.y < resizeBorder;
        const bool bottom = point.y >= bounds.bottom - resizeBorder;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;

        const int commandBarHeight = MulDiv(40, static_cast<int>(dpi), 96);
        const int captionButtonsWidth = MulDiv(138, static_cast<int>(dpi), 96);
        if (point.y < commandBarHeight && point.x >= MulDiv(260, static_cast<int>(dpi), 96) &&
            point.x < bounds.right - captionButtonsWidth)
        {
            return HTCAPTION;
        }
    }

    return DefWindowProcW(mWindowHandle, message, wParam, lParam);
}

float Win32Window::GetContentScale() const
{
    if (!mWindowHandle)
    {
        return 1.0f;
    }
    const UINT dpi = GetDpiForWindow(mWindowHandle);
    return dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;
}

}
