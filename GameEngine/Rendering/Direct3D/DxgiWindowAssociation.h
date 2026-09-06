#pragma once

#include <dxgi.h>
#include <windows.h>
#include <wrl/client.h>

#include "../../Platform/Win32/Win32Diagnostics.h"

namespace GameEngine::Rendering::Direct3D
{

/// <summary>
/// 스왑 체인이 붙은 창에 대해 DXGI가 메시지 큐를 감시하지 않게 한다.
///
/// 기본값에서 DXGI는 창의 메시지 큐를 지켜보며 모드 전환과 Alt+Enter에 반응한다. 그 감시는
/// present를 부르는 스레드와 메시지를 펌프하는 스레드가 같을 때를 전제하므로, 렌더링이 자기
/// 스레드로 나가면 두 스레드가 서로를 기다리는 자리가 된다.
/// <c>DXGI_MWA_NO_WINDOW_CHANGES</c>가 그 감시를 끄고, <c>DXGI_MWA_NO_ALT_ENTER</c>는 전체화면
/// 전환까지 애플리케이션의 것으로 남긴다. 이 엔진은 창 상태를 스스로 정하므로 둘 다 끈다.
///
/// 팩토리를 인수로 받지 않고 스왑 체인에게 자기 부모를 묻는 이유는, 이 호출이 <b>그 스왑 체인을
/// 만든 팩토리</b>에서 일어나야 효과가 있기 때문이다. 스왑 체인을 만들면서 팩토리를 안 돌려주는
/// 생성 함수도 있어서, 부르는 쪽이 쥔 팩토리가 그것과 다를 수 있다.
/// </summary>
/// <param name="swapChain">창에 present하는 스왑 체인이다.</param>
/// <param name="windowHandle">그 스왑 체인의 출력 창이다.</param>
/// <returns>감시를 껐으면 true이며, 실패는 로그로 말한다.</returns>
[[nodiscard]] inline bool DisableDxgiWindowMonitoring(
    IDXGISwapChain* const swapChain, const HWND windowHandle)
{
    if (!swapChain || !windowHandle)
    {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory> factory;
    HRESULT result = swapChain->GetParent(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        Platform::Win32::LogHResult("Failed to reach the swap chain's DXGI factory", result);
        return false;
    }

    result = factory->MakeWindowAssociation(
        windowHandle, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(result))
    {
        Platform::Win32::LogHResult("Failed to configure the DXGI window association", result);
        return false;
    }
    return true;
}

}
