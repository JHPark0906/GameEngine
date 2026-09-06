#pragma once

namespace GameEngine::Platform
{

/// <summary>네이티브 표면이 속한 윈도잉 시스템이다.</summary>
enum class NativeSurfaceKind : unsigned char
{
    /// <summary>표면이 없다. 그래픽 장치는 여기에 present할 수 없다.</summary>
    None = 0,
    /// <summary>Win32 창이다. `handle`은 HWND다.</summary>
    Win32,
};

/// <summary>
/// 그래픽 백엔드가 present할 수 있는 그리기 대상이다.
///
/// kind가 필드들이 어느 윈도잉 시스템에 속하는지 말하므로, 백엔드는 지원하지 않는 표면을
/// 거부할 수 있다. 그리기 대상을 식별하는 값은 여러 개일 수 있다. X11은 display와 window,
/// Wayland는 display와 surface가 필요하므로 포인터 하나로 제한하지 않는다.
/// </summary>
struct NativeSurface
{
    NativeSurfaceKind kind = NativeSurfaceKind::None;

    /// <summary>Win32는 HWND, Xlib은 Window, Wayland는 wl_surface*이다.</summary>
    void* handle = nullptr;

    /// <summary>서버 또는 디스플레이 연결이다. Win32에서는 쓰지 않는다.</summary>
    void* connection = nullptr;

    [[nodiscard]] bool IsValid() const
    {
        return kind != NativeSurfaceKind::None && handle != nullptr;
    }

    /// <summary>HWND에 대한 표면을 만든다.</summary>
    [[nodiscard]] static NativeSurface FromWin32Window(void* const windowHandle)
    {
        return { NativeSurfaceKind::Win32, windowHandle, nullptr };
    }
};

}
