#include "DiagnosticsTests.h"

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

#include "Diagnostics/ApiFailure.h"
#include "Diagnostics/Debug.h"
#include "Platform/Win32/Win32Diagnostics.h"
#include "Rendering/Direct3D/DeviceRemoval.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
}

/// <summary>
/// 모든 API가 실패를 같은 모양으로 보고한다.
///
/// 유일한 도우미가 HRESULT 모양이라서, 다른 종류의 코드를 반환하는 API의 백엔드는 자기 것을
/// 썼을 테고 실행 로그는 어느 백엔드가 만들었는지에 따라 다르게 읽혔을 것이다. 여기서 렌더링
/// 문제는 실행 로그를 읽어 진단되므로, 형식은 공유되고 API마다 남는 것은 코드 해독뿐이다.
/// </summary>
bool RunApiFailureFormatTests()
{
    std::vector<std::string> captured;
    const GameEngine::Diagnostics::Debug::LogListenerId listener =
        GameEngine::Diagnostics::Debug::AddLogListener(
            [&captured](const GameEngine::Diagnostics::LogEntry& entry)
            {
                captured.push_back(entry.message);
            });

    // One caller decodes an HRESULT, another a made-up code from a different API.
    GameEngine::Platform::Win32::LogHResult("Creating something", E_INVALIDARG);
    GameEngine::Diagnostics::LogApiFailure("Creating something", "Vulkan", 0x2Aull);

    GameEngine::Diagnostics::Debug::RemoveLogListener(listener);

    if (captured.size() < 2)
    {
        return Expect(false, "both failure reports should reach a log listener");
    }
    const std::string& hresultReport = captured[captured.size() - 2];
    const std::string& otherReport = captured[captured.size() - 1];

    const bool sharedShape =
        hresultReport.find("Creating something failed. api=") != std::string::npos &&
        otherReport.find("Creating something failed. api=") != std::string::npos &&
        hresultReport.find(", code=0x") != std::string::npos &&
        otherReport.find(", code=0x") != std::string::npos;

    return Expect(
               sharedShape,
               "an HRESULT and another API's code should be reported in the same shape") &&
        Expect(
            hresultReport.find("api=Direct3D") != std::string::npos &&
                otherReport.find("api=Vulkan") != std::string::npos,
            "each report should name the API that rejected the call") &&
        Expect(otherReport.find("2a") != std::string::npos, "the code should be shown in hex");
}

/// <summary>
/// 장치 손실 로그가 GetDeviceRemovedReason의 사유를 포함하고 제거와 리셋을 구분하는지 확인한다.
/// </summary>
bool RunDeviceRemovalReportTests()
{
    using GameEngine::Rendering::Direct3D::IsDeviceLost;
    using GameEngine::Rendering::Direct3D::ReportDeviceRemoval;

    const bool recognizesLoss = IsDeviceLost(DXGI_ERROR_DEVICE_REMOVED) &&
        IsDeviceLost(DXGI_ERROR_DEVICE_RESET) && IsDeviceLost(DXGI_ERROR_DEVICE_HUNG) &&
        !IsDeviceLost(E_INVALIDARG) && !IsDeviceLost(S_OK);

    std::vector<std::string> captured;
    const GameEngine::Diagnostics::Debug::LogListenerId listener =
        GameEngine::Diagnostics::Debug::AddLogListener(
            [&captured](const GameEngine::Diagnostics::LogEntry& entry)
            {
                captured.push_back(entry.message);
            });

    // present가 "장치가 없다"까지만 말하고, 장치가 구체적인 사유를 답하는 실제 모양이다.
    const bool reportedRemoval = ReportDeviceRemoval(
        "Direct3D 11", DXGI_ERROR_DEVICE_REMOVED, DXGI_ERROR_DEVICE_HUNG);
    const bool reportedReset =
        ReportDeviceRemoval("Direct3D 12", DXGI_ERROR_DEVICE_RESET, S_OK);
    // 장치 손실이 아닌 실패는 이 보고의 몫이 아니다.
    const bool ignoredOther = ReportDeviceRemoval("Direct3D 12", E_INVALIDARG, S_OK);

    GameEngine::Diagnostics::Debug::RemoveLogListener(listener);

    if (captured.size() < 2)
    {
        return Expect(false, "each device loss should reach a log listener");
    }
    const std::string& hungReport = captured[captured.size() - 2];
    const std::string& resetReport = captured[captured.size() - 1];

    return Expect(recognizesLoss, "device-loss codes should be told apart from other failures") &&
        Expect(
            reportedRemoval && reportedReset && !ignoredOther,
            "only a device loss should be reported as one") &&
        Expect(
            captured.size() == 2,
            "a failure that is not a device loss should not be reported as one") &&
        Expect(
            hungReport.find("api=Direct3D 11") != std::string::npos &&
                hungReport.find("hung") != std::string::npos,
            "the device's own reason should win over the code the operation returned") &&
        Expect(
            resetReport.find("api=Direct3D 12") != std::string::npos &&
                resetReport.find("reset") != std::string::npos,
            "a reset should read differently from a removal");
}

static const TestSupport::Registration gApiFailureFormatTests{
    "Diagnostics", "api failure format tests should pass", RunApiFailureFormatTests };

static const TestSupport::Registration gDeviceRemovalReportTests{
    "Diagnostics", "device removal report tests should pass", RunDeviceRemovalReportTests };
