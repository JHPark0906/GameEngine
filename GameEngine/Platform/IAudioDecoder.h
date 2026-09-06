#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace GameEngine::Platform
{

/// <summary>플랫폼 디코더가 만든 인터리브된 모노/스테레오 16비트 PCM이다.</summary>
struct DecodedAudio
{
    std::uint32_t channelCount = 0;
    std::uint32_t sampleRate = 0;
    std::vector<std::int16_t> samples;

    [[nodiscard]] bool IsValid() const
    {
        return (channelCount == 1 || channelCount == 2) && sampleRate > 0 &&
            !samples.empty() && samples.size() % channelCount == 0;
    }
};

/// <summary>압축 오디오가 무제한 PCM 메모리로 커지지 않게 호출자가 정하는 한계다.</summary>
struct AudioDecodeLimits
{
    std::size_t maximumDecodedBytes = 64ull * 1024ull * 1024ull;
};

/// <summary>플랫폼의 압축 오디오 디코더다. 현재 지원 형식은 MP3이다.</summary>
class IAudioDecoder
{
public:
    virtual ~IAudioDecoder() = default;
    IAudioDecoder(const IAudioDecoder&) = delete;
    IAudioDecoder& operator=(const IAudioDecoder&) = delete;
    IAudioDecoder(IAudioDecoder&&) = delete;
    IAudioDecoder& operator=(IAudioDecoder&&) = delete;

    /// <summary>
    /// 파일 경로 없이 메모리의 인코딩된 바이트를 PCM으로 바꾼다. 성공하면 audio를 교체하고
    /// error를 비운다. 실패하면 audio는 그대로 두고 error에 이유를 쓴다. 호출 스레드에 필요한
    /// 플랫폼 초기화와 해제도 이 호출 안에서 끝나므로, 별도 Initialize는 필요하지 않다.
    /// </summary>
    [[nodiscard]] virtual bool Decode(std::span<const std::byte> encodedBytes,
        const AudioDecodeLimits& limits, DecodedAudio& audio, std::string& error) = 0;

protected:
    IAudioDecoder() = default;
};

}
