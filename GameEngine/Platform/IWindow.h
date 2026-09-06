#pragma once

#include <string>

#include "IInput.h"
#include "NativeSurface.h"

namespace GameEngine::Platform
{

/// <summary>창의 제목 표시줄과 메뉴 표시 방식을 지정한다.</summary>
enum class WindowChrome : unsigned char
{
    /// <summary>운영 체제가 제공하는 제목 표시줄과 메뉴를 사용한다.</summary>
    System = 0,
    /// <summary>애플리케이션이 제목 표시줄과 명령 모음을 직접 그린다.</summary>
    Custom,
};

/// <summary>창의 색상 모드이다.</summary>
enum class WindowTheme : unsigned char
{
    /// <summary>운영 체제의 기본 모드를 따른다.</summary>
    System = 0,
    /// <summary>밝은 모드를 사용한다.</summary>
    Light,
    /// <summary>어두운 모드를 사용한다.</summary>
    Dark,
};

/// <summary>플랫폼 창이 만들어질 때 어떤 모습이어야 하는지이다.</summary>
struct WindowDescription
{
    std::wstring title;
    int clientWidth = 1280;
    int clientHeight = 720;
    WindowChrome chrome = WindowChrome::System;
    WindowTheme theme = WindowTheme::System;
};

/// <summary>한 번의 플랫폼 메시지 처리 결과를 나타낸다.</summary>
enum class WindowMessageResult : unsigned char
{
    /// <summary>처리할 메시지가 없는 상태이다.</summary>
    Idle,
    /// <summary>메시지 하나를 처리한 상태이다.</summary>
    Processed,
    /// <summary>
    /// 사람이 창을 닫으려 한 상태다. 창은 아직 살아 있다 — 이 결과를 받은 쪽이 닫을지 정하고,
    /// 닫기로 했으면 <see cref="RequestClose"/>를 부른다.
    ///
    /// 창이 스스로 닫지 않는 이유는 저장되지 않은 작업 때문이다. 닫을지는 무엇이 저장되지
    /// 않았는지 아는 쪽만 답할 수 있고, 그 물음을 창 프로시저 안에서 하면 메시지 처리 도중에
    /// 답을 기다리게 된다.
    /// </summary>
    CloseRequested,
    /// <summary>종료 메시지를 받은 상태이다.</summary>
    Quit,
};

/// <summary>
/// 애플리케이션의 창과 메시지 펌프이다. 뒤의 윈도잉 시스템과 무관하다. 렌더링 계층은 이 타입을
/// 결코 보지 않는다. 창이 내놓는 표면만 받는다.
/// </summary>
class IWindow
{
public:
    virtual ~IWindow() = default;

    IWindow(const IWindow&) = delete;
    IWindow& operator=(const IWindow&) = delete;
    IWindow(IWindow&&) = delete;
    IWindow& operator=(IWindow&&) = delete;

    /// <summary>주어진 속성이 기술하는 창을 만든다.</summary>
    [[nodiscard]] virtual bool Initialize(const WindowDescription& description) = 0;

    /// <summary>대기 중인 플랫폼 메시지 하나를 처리한다.</summary>
    /// <param name="exitCode">종료 메시지가 도착하면 프로세스 종료 코드를 받는다.</param>
    [[nodiscard]] virtual WindowMessageResult ProcessMessage(int& exitCode) const = 0;

    /// <summary>그래픽 장치가 present하는 표면이다. 초기화 전에는 무효다.</summary>
    [[nodiscard]] virtual NativeSurface GetSurface() const = 0;

    /// <summary>
    /// 승인된 종료를 요청한다. 다음 <see cref="ProcessMessage"/>가 종료를 답한다.
    /// 표면은 창 소유자가 렌더링을 종료하고 창을 해제할 때까지 유지한다.
    /// </summary>
    virtual void RequestClose() = 0;

    /// <summary>
    /// 창을 모니터 전체로 펼치거나 원래 자리로 되돌린다.
    ///
    /// 테두리 없는 창이지 그래픽 API의 독점 모드가 아니다. 화면 해상도는 그대로이고 바뀌는 것은
    /// 창의 스타일과 사각형뿐이라, 스왑 체인은 크기 변화를 평소의 길로 받는다 — 그래서 이
    /// 전환은 백엔드를 하나도 알지 못하고, 백엔드 쪽에도 전환을 아는 코드가 없다.
    /// </summary>
    /// <param name="fullscreen">펼칠지 여부다.</param>
    /// <returns>요청한 상태가 되었으면 true이다.</returns>
    [[nodiscard]] virtual bool SetFullscreen(bool fullscreen) = 0;

    /// <summary>지금 전체화면인지 여부이다.</summary>
    [[nodiscard]] virtual bool IsFullscreen() const = 0;

    /// <summary>
    /// 이 창이 받는 그대로의 키보드와 마우스이다. 엔진이 겨냥하는 모든 시스템에서 입력은 창에
    /// 속한다 — 도착하는 것은 이 창이 포커스를 가졌기에 도착한다 — 그래서 어느 창이 앞에 있는지와
    /// 엔진이 보조를 맞춰야 하는 별도 장치가 아니라 창을 통해 닿는다.
    /// </summary>
    [[nodiscard]] virtual IInput& GetInput() = 0;

    /// <summary>이미 만들어진 표면에 명시적 밝은/어두운 모드를 적용한다.</summary>
    [[nodiscard]] virtual bool SetTheme(const NativeSurface& surface, WindowTheme theme) const = 0;

    /// <summary>
    /// 이 창이 놓인 화면의 콘텐츠 배율이다. 96 DPI 화면에서 1.0, 그 두 배로 조밀한 화면에서
    /// 2.0이다. 렌더 타깃과 커서 좌표는 물리 픽셀이므로, 사람에게 같은 크기로 보여야 하는 것 —
    /// UI의 글자와 위젯 — 만 이 배율을 곱한다. 창이 모니터 사이를 옮겨 가면 값이 바뀌므로
    /// 프레임마다 물어도 된다.
    /// </summary>
    [[nodiscard]] virtual float GetContentScale() const { return 1.0f; }

protected:
    IWindow() = default;
};

}
