#include "pch.h"

#include <windows.h>

#if defined(DEBUG) || defined(_DEBUG)
#include <crtdbg.h>
#endif

#include "../App/PlayerMain.h"
#include "../Platform/Win32/Win32Diagnostics.h"

/// <summary>
/// Win32 프로세스 진입점이다.
///
/// 플랫폼 특유의 것은 운영 체제가 부르는 심벌과 그 운영 체제에 필요한 시동뿐이다. 플레이어가
/// 실제로 하는 모든 일은 App::RunPlayer에 있고, 그것은 어떤 플랫폼 API의 이름도 부르지 않는다.
/// 포팅은 자기 플랫폼 디렉터리에 자기 진입 번역 단위를 넣고 — 예컨대 POSIX 호스트라면 `main` —
/// 빌드가 이 디렉터리 대신 그 디렉터리를 컴파일한다. 어느 플랫폼을 겨냥할지는 그래픽 백엔드와
/// 달리 컴파일 시점의 선택인데, 하나의 바이너리에 두 플랫폼 호스트가 공존하며 런타임 탐지 하나가
/// 고르게 할 수는 없기 때문이다.
/// </summary>
int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
#if defined(DEBUG) || defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    // 디버거 미러링은 플랫폼의 편의라서, 플랫폼 전용인 이 진입 번역 단위가 설치한다.
    GameEngine::Platform::Win32::InstallDebuggerLogMirror();
    // 파일 미러도 마찬가지다 — 디버거도 콘솔도 없는 실행(자동화, 사용자가 그냥 아이콘을 두 번
    // 누른 경우)에서 창이 뜨기 전에 실패하면 이것이 실패 이유를 남기는 유일한 곳이다. 이
    // 프로세스가 사는 동안은 걷어낼 일이 없으므로 id는 버린다.
    static_cast<void>(GameEngine::Platform::Win32::InstallLogFileMirror());

    // 모든 프로젝트가 DPI를 인지한다: 창은 논리 크기에 배율을 곱해 열리고, 렌더 타깃은 물리
    // 픽셀이며, UI는 GetContentScale로 따라간다. 매니페스트가 아니라 여기서 정하므로 프로젝트가
    // 각자 매니페스트를 갖지 않아도 된다.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    return GameEngine::App::RunPlayer();
}
