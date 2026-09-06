#pragma once

#include <dxgi.h>
#include <winerror.h>

#include <ios>

#include "../../Diagnostics/Debug.h"

namespace GameEngine::Rendering::Direct3D
{

/// <summary>
/// 장치 손실 HRESULT를 사람이 읽는 사유로 옮긴다.
///
/// 사유를 구분해 적는 것이 이 함수가 존재하는 이유다. 제거(REMOVED)와 리셋(RESET)과 행(HUNG)은
/// 원인이 서로 다르고 — 드라이버 교체·GPU 리셋·이쪽이 보낸 작업의 무한 루프 — 대응도 다르지만,
/// 코드만 남기면 그 셋이 같은 줄로 읽힌다.
/// </summary>
/// <param name="reason">장치가 보고한 제거 사유이다.</param>
/// <returns>사유를 설명하는 문장이다.</returns>
[[nodiscard]] inline const char* DescribeDeviceRemovalReason(const HRESULT reason)
{
    switch (reason)
    {
    case DXGI_ERROR_DEVICE_REMOVED:
        return "the device was removed (a driver update, a disabled or unplugged adapter)";
    case DXGI_ERROR_DEVICE_RESET:
        return "the device was reset after an application error";
    case DXGI_ERROR_DEVICE_HUNG:
        return "the device hung on work this application submitted";
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
        return "the driver failed internally";
    case DXGI_ERROR_INVALID_CALL:
        return "the driver rejected a call as invalid";
    case E_OUTOFMEMORY:
        return "the device ran out of memory";
    case S_OK:
        return "the device reports no removal reason";
    default:
        return "the device reported an unrecognized reason";
    }
}

/// <summary>이 HRESULT가 장치 손실을 뜻하는지이다.</summary>
/// <param name="result">API가 반환한 코드이다.</param>
/// <returns>장치가 사라졌거나 리셋되었으면 true이다.</returns>
[[nodiscard]] inline bool IsDeviceLost(const HRESULT result)
{
    return result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
        result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

/// <summary>
/// 실패한 연산이 장치 손실이었는지 판정하고, 맞으면 사유를 로그로 남긴다.
///
/// 사유를 남기지 않으면 드라이버 업데이트 한 번이 프레임 실패 → 프로세스 종료로 이어지면서
/// 무엇이 죽였는지조차 남지 않는다. 이 함수는 복구하지 않는다: 복구는 별개의 결정이고,
/// 여기까지가 진단이다.
/// </summary>
/// <param name="api">보고하는 백엔드의 이름이다. 예: "Direct3D 12".</param>
/// <param name="operationResult">실패한 연산이 반환한 코드이다.</param>
/// <param name="removalReason">장치가 답한 제거 사유이다(<c>GetDeviceRemovedReason</c>).</param>
/// <returns>장치 손실이었으면 true이다.</returns>
inline bool ReportDeviceRemoval(
    const char* const api, const HRESULT operationResult, const HRESULT removalReason)
{
    const bool lost = IsDeviceLost(operationResult) || IsDeviceLost(removalReason);
    if (!lost)
    {
        return false;
    }
    // 장치가 답한 사유가 더 구체적이다. 연산이 돌려준 코드는 "장치가 없다"까지만 말한다.
    const HRESULT reason = IsDeviceLost(removalReason) ? removalReason : operationResult;
    Diagnostics::Debug::LogError(
        "The graphics device was lost. api=", api, ", reason=",
        DescribeDeviceRemovalReason(reason), ", code=0x", std::hex,
        static_cast<unsigned int>(reason));
    return true;
}

}
