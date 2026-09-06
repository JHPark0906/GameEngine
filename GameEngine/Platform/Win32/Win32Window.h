#pragma once

#include <windows.h>

#include "../IWindow.h"
#include "../WindowPlacement.h"
#include "Win32Input.h"

namespace GameEngine::Platform::Win32
{

/// <summary>Win32 창의 등록, 생성, 메시지 처리 및 수명을 관리한다.</summary>
class Win32Window final : public IWindow
{
public:
    Win32Window();
    ~Win32Window() override;

    /// <summary>창 클래스를 등록하고 지정한 클라이언트 크기의 창을 생성한다.</summary>
    [[nodiscard]] bool Initialize(const WindowDescription& description) override;

    /// <summary>대기 중인 Win32 메시지 하나를 처리한다.</summary>
    [[nodiscard]] WindowMessageResult ProcessMessage(int& exitCode) const override;
    void RequestClose() override;

    /// <summary>그래픽 장치가 출력 대상으로 사용할 창 표면을 반환한다.</summary>
    [[nodiscard]] NativeSurface GetSurface() const override;

    /// <summary>창을 모니터 전체로 펼치거나 기억해 둔 자리로 되돌린다.</summary>
    [[nodiscard]] bool SetFullscreen(bool fullscreen) override;

    [[nodiscard]] bool IsFullscreen() const override { return mFullscreen; }

    /// <summary>이 창이 받은 키보드와 마우스 상태를 반환한다.</summary>
    [[nodiscard]] IInput& GetInput() override { return mInput; }

    /// <summary>이미 생성된 엔진 창의 밝기 모드를 변경한다.</summary>
    [[nodiscard]] bool SetTheme(const NativeSurface& surface, WindowTheme theme) const override;

    /// <summary>창의 DPI를 96으로 나눈 값이다. 창이 없으면 1.0이다.</summary>
    [[nodiscard]] float GetContentScale() const override;

private:
    enum class QuitState : unsigned char
    {
        None,
        Pending,
        Reported
    };

    [[nodiscard]] bool RegisterWindowClass() const;
    void ApplyWindowTheme();
    [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WindowProcedure(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);

    static constexpr const wchar_t* WindowClassName = L"GameEngineWindow";

    HINSTANCE mInstanceHandle = nullptr;
    HWND mWindowHandle = nullptr;
    Win32Input mInput;
    WindowChrome mWindowChrome = WindowChrome::System;
    WindowTheme mWindowTheme = WindowTheme::System;

    /// <summary>
    /// 전체화면 상태와 창 모드로 돌아갈 자리다. 저장하지 않으므로 다음 실행은 창 모드로 시작한다.
    /// 시작 모드 설정은 제공하지 않는다.
    /// </summary>
    bool mFullscreen = false;
    /// <summary>사람이 창을 닫으려 했고 아직 답을 받지 못한 상태다. 창은 살아 있다.</summary>
    mutable bool mCloseRequested = false;
    // Approved close requests belong to this window, not the thread's message queue.
    mutable QuitState mQuitState = QuitState::None;
    bool mDestroying = false;
    DWORD mWindowedStyle = 0;
    WindowPlacementMemory mPlacementMemory;
};

}
