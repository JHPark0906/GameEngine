#pragma once

#include <cstddef>
#include <span>
#include <string>

#include "../IAudioDecoder.h"

namespace GameEngine::Platform::Win32
{

/// <summary>Windows Media Foundation으로 메모리에 있는 MP3를 PCM16으로 디코딩한다.</summary>
class Win32AudioDecoder final : public IAudioDecoder
{
public:
    [[nodiscard]] bool Decode(std::span<const std::byte> encodedBytes,
        const AudioDecodeLimits& limits, DecodedAudio& audio, std::string& error) override;
};

}
