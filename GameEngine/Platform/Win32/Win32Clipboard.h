#pragma once

#include <string>
#include <string_view>

#include "../IClipboard.h"

namespace GameEngine::Platform::Win32
{

/// <summary>Win32 클립보드다. CF_UNICODETEXT로 오가고, 경계에서 UTF-8과 UTF-16을 오간다.</summary>
class Win32Clipboard final : public IClipboard
{
public:
    Win32Clipboard() = default;

    [[nodiscard]] std::string GetText() override;
    void SetText(std::string_view text) override;
};

}
