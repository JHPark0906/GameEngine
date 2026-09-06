#pragma once

#include <cstdint>
#include <iomanip>

#include "Debug.h"

namespace GameEngine::Diagnostics
{

/// <summary>
/// 그래픽 또는 플랫폼 API로의 실패한 호출을, 모든 API에 대해 한 가지 형식으로 보고한다.
///
/// 코드 해독은 호출자의 일이다. 그것이 어떤 종류의 코드인지 — HRESULT인지 VkResult인지
/// errno인지 — 는 호출자만 알기 때문이다. 반대로 보고 형식 만들기는 호출자의 일이 아니다.
/// 도우미가 한 API의 코드 모양에 묶여 있으면 다른 코드를 반환하는 백엔드는 자기 형식을 쓰게
/// 되고, 실행 로그가 어느 백엔드가 썼는지에 따라 다르게 읽힌다. 렌더링 문제는 그 로그를 읽어
/// 진단되므로 그 차이는 중요하다.
/// </summary>
/// <param name="operation">시도한 일이다. 메시지가 문장으로 읽히도록 표현한다.</param>
/// <param name="api">그것을 거부한 API이다. 예: "Direct3D 12", "Vulkan".</param>
/// <param name="code">API가 반환한 값이다. 16진수로 표시된다.</param>
inline void LogApiFailure(
    const char* const operation, const char* const api, const std::uint64_t code)
{
    Debug::LogError(operation, " failed. api=", api, ", code=0x", std::hex, code);
}

/// <summary>
/// 호출자가 이미 해독한 설명과 함께 실패한 API 호출을 보고한다. 코드를 읽을 수 있는 텍스트로
/// 바꿀 수 있는 API를 위한 것이다.
/// </summary>
inline void LogApiFailure(
    const char* const operation,
    const char* const api,
    const std::uint64_t code,
    const char* const description)
{
    Debug::LogError(
        operation, " failed. api=", api, ", code=0x", std::hex, code, ", ", description);
}

}
