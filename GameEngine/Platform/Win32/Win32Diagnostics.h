#pragma once

#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "../../Diagnostics/ApiFailure.h"
#include "../PlatformServices.h"

namespace GameEngine::Platform::Win32
{

/// <summary>
/// 실패한 Direct3D 또는 COM 연산을 그 HRESULT와 함께 보고한다.
///
/// 이것은 `Diagnostics::LogApiFailure`의 HRESULT 해독 가장자리일 뿐이고 형식 자체는 공유되므로,
/// 다른 종류의 코드를 반환하는 API의 백엔드도 자기만의 형식을 지어내는 대신 같은 방식으로 읽히는
/// 보고를 만든다.
/// </summary>
/// <param name="operation">시도한 일이다. 메시지가 문장으로 읽히도록 표현한다.</param>
inline void LogHResult(const char* const operation, const HRESULT result)
{
    Diagnostics::LogApiFailure(
        operation, "Direct3D", static_cast<std::uint64_t>(static_cast<unsigned int>(result)));
}

/// <summary>
/// 로그를 붙어 있는 디버거로도 미러링하는 리스너를 설치한다.
///
/// 디버거 미러링은 플랫폼의 편의이지 로깅의 일부가 아니다. 그래서 Debug 자신이
/// OutputDebugString을 부르지 않고, 플랫폼 진입점이 — 어차피 Win32 전용인 그 번역 단위가 —
/// 리스너로 설치한다. 설치 전의 로그는 미러되지 않는데, 그때는 디버거에 보여줄 사람도 아직
/// 없다.
/// </summary>
inline void InstallDebuggerLogMirror()
{
    static_cast<void>(Diagnostics::Debug::AddLogListener(
        [](const Diagnostics::LogEntry& entry)
        {
            const std::string formatted =
                "[" + std::string(Diagnostics::Debug::GetLogLevelName(entry.level)) + "] " +
                entry.message + '\n';
            OutputDebugStringA(formatted.c_str());
        }));
}

/// <summary>
/// 로그를 실행 파일 옆의 파일로도 미러링하는 리스너를 설치한다.
///
/// 디버거 미러링과 같은 이유로 같은 자리에 있다: Debug 자신은 어디에 쓸지 모르고, 아는 것은
/// 플랫폼 진입점뿐이다. 이 미러가 있는 이유는 디버거 미러가 못 하는 것을 하기 위해서다 —
/// 자동화나 사람이 창도 못 보고 죽는 실행을 붙잡을 때는 디버거가 붙어 있지 않고, 콘솔도 없는
/// GUI 서브시스템 앱이라 표준 오류 스트림도 아무도 안 본다. 파일은 붙어 있는 것이 아무것도
/// 없어도 남는다.
///
/// 매 실행마다 새로 쓴다(append가 아니라 truncate) — 오래된 실행의 오류가 남아 있으면 이번
/// 실행이 실패한 이유처럼 읽힌다. 그래서 이 파일은 언제나 <b>가장 최근 실행 하나</b>만 말한다.
/// 파일을 못 열면(배포 디렉터리가 읽기 전용인 경우 등) 조용히 설치를 건너뛴다 — 진단 편의가
/// 없다고 앱이 뜨지 않아야 할 이유는 없다.
///
/// 디버거 미러와 달리 리스너 id를 돌려준다. 실행 파일 하나가 켜져 있는 동안은 아무도 이것을
/// 걷어낼 필요가 없지만, 시험은 자기가 이 함수를 부른 흔적을 그 실행이 쓴 시험 실행 파일 옆의
/// 로그 파일에 남기므로 — 다음 시험들의 로그까지 계속 그 파일에 쌓이지 않도록 스스로 치운다.
/// </summary>
[[nodiscard]] inline Diagnostics::Debug::LogListenerId InstallLogFileMirror()
{
    std::filesystem::path logPath = PlatformServices::GetExecutablePath();
    logPath.replace_extension(L".log");

    auto file = std::make_shared<std::ofstream>(logPath, std::ios::trunc);
    if (!file->is_open())
    {
        return 0;
    }

    return Diagnostics::Debug::AddLogListener(
        [file](const Diagnostics::LogEntry& entry)
        {
            *file << "[" << Diagnostics::Debug::GetLogLevelName(entry.level) << "] "
                  << entry.message << '\n';
            file->flush();
        });
}

}
