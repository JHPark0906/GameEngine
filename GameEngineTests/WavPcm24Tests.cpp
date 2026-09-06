#include "WavPcm24Tests.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "Assets/WavAudio.h"
#include "TestSupport.h"

namespace
{
    using Bytes = std::vector<std::byte>;

    template<typename T>
    void Append(Bytes& bytes, const T value)
    {
        const auto* first = reinterpret_cast<const std::byte*>(&value);
        bytes.insert(bytes.end(), first, first + sizeof(T));
    }

    Bytes Wav(const std::uint16_t channels, const std::initializer_list<std::int32_t> samples)
    {
        const std::uint32_t sampleBytes = static_cast<std::uint32_t>(samples.size() * 3);
        Bytes bytes;
        const auto tag = [&bytes](const char* value)
        {
            for (int index = 0; index < 4; ++index) bytes.push_back(static_cast<std::byte>(value[index]));
        };
        tag("RIFF");
        Append(bytes, 36u + sampleBytes + sampleBytes % 2);
        tag("WAVE");
        tag("fmt ");
        Append(bytes, std::uint32_t{ 16 });
        Append(bytes, std::uint16_t{ 1 });
        Append(bytes, channels);
        Append(bytes, std::uint32_t{ 48000 });
        Append(bytes, static_cast<std::uint32_t>(48000u * channels * 3));
        Append(bytes, static_cast<std::uint16_t>(channels * 3));
        Append(bytes, std::uint16_t{ 24 });
        tag("data");
        Append(bytes, sampleBytes);
        for (const auto sample : samples)
        {
            const auto bits = static_cast<std::uint32_t>(sample);
            bytes.push_back(static_cast<std::byte>(bits & 0xffu));
            bytes.push_back(static_cast<std::byte>((bits >> 8) & 0xffu));
            bytes.push_back(static_cast<std::byte>((bits >> 16) & 0xffu));
        }
        if (sampleBytes % 2 != 0) bytes.push_back(std::byte{});
        return bytes;
    }

    template<typename T>
    void Replace(Bytes& bytes, const std::size_t offset, const T value)
    {
        std::memcpy(bytes.data() + offset, &value, sizeof(T));
    }
}

bool RunWavPcm24Tests()
{
    using namespace GameEngine::Assets;
    using TestSupport::Expect;
    bool passed = true;
    for (const std::uint16_t channels : { std::uint16_t{ 1 }, std::uint16_t{ 2 } })
    {
        const auto bytes = Wav(channels, { -8388608, 8388607, -256, -1, 0, 255, 256, 65536 });
        DecodedWavAudio decoded;
        std::string error = "old failure";
        passed &= Expect(DecodeWavAudio(bytes, decoded, error) && error.empty() &&
            decoded.channelCount == channels && decoded.sampleRate == 48000 &&
            decoded.samples == std::vector<std::int16_t>{ -32768, 32767, -1, -1, 0, 0, 1, 256 },
            "PCM24 must preserve signed endpoints, channel order and frame count when dropping the low byte");
    }
    {
        DecodedWavAudio decoded;
        std::string error;
        passed &= Expect(DecodeWavAudio(Wav(1, { 65536 }), decoded, error) &&
            decoded.samples == std::vector<std::int16_t>{ 256 },
            "a valid odd-sized mono PCM24 data chunk must accept its alignment pad");
    }
    for (int scenario = 0; scenario < 7; ++scenario)
    {
        Bytes bytes = Wav(2, { -8388608, 8388607 });
        if (scenario == 0) Replace(bytes, 32, std::uint16_t{ 4 });
        if (scenario == 1) Replace(bytes, 28, std::uint32_t{ 48000 * 4 });
        if (scenario == 2)
        {
            Replace(bytes, 40, std::uint32_t{ 5 });
            bytes.pop_back();
        }
        if (scenario == 3) bytes = Wav(2, { 65536 }); // Complete sample, incomplete stereo frame.
        if (scenario == 4) Replace(bytes, 24, std::uint32_t{ 0xffffffffu });
        if (scenario == 5) Replace(bytes, 22, std::uint16_t{ 3 });
        if (scenario == 6) bytes.pop_back(); // Chunk declares more bytes than the file holds.
        DecodedWavAudio decoded{ 1, 8000, { 123, -456 } };
        std::string error;
        passed &= Expect(!DecodeWavAudio(bytes, decoded, error) && !error.empty() &&
            decoded.channelCount == 1 && decoded.sampleRate == 8000 &&
            decoded.samples == std::vector<std::int16_t>{ 123, -456 },
            "invalid PCM24 format/frame bounds must fail while preserving the previous decoded output");
    }
    return passed;
}

static const TestSupport::Registration gWavPcm24Tests{
    "Audio", "PCM24 WAV decoding tests should pass", RunWavPcm24Tests };
