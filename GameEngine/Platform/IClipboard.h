#pragma once

#include <string>
#include <string_view>

namespace GameEngine::Platform
{

/// <summary>
/// 시스템 클립보드의 텍스트 면이다. UTF-8만 오간다 — 이 계층 위의 모든 텍스트가 그렇듯이.
///
/// 클립보드는 창처럼 플랫폼 설비라서 인터페이스로 건넌다: UI의 텍스트 편집이 복사·붙여넣기를
/// 하되 어느 윈도잉 시스템의 클립보드인지 몰라야 하고, 테스트는 시스템 클립보드 — 프로세스
/// 밖의 전역 상태 — 를 건드리는 대신 가짜를 꽂아야 한다.
/// </summary>
class IClipboard
{
public:
    virtual ~IClipboard() = default;

    IClipboard(const IClipboard&) = delete;
    IClipboard& operator=(const IClipboard&) = delete;
    IClipboard(IClipboard&&) = delete;
    IClipboard& operator=(IClipboard&&) = delete;

    /// <summary>클립보드의 텍스트다. 텍스트가 없거나 읽을 수 없으면 빈 문자열이다.</summary>
    [[nodiscard]] virtual std::string GetText() = 0;

    /// <summary>클립보드를 이 텍스트로 바꾼다.</summary>
    virtual void SetText(std::string_view text) = 0;

protected:
    IClipboard() = default;
};

}
