#include "pch.h"
#include "WavAudio.h"

#include <cstring>
#include <utility>

namespace GameEngine::Assets
{

namespace
{
    /// <summary>바이트 열에서 리틀 엔디언 정수를 읽는다. WAV의 모든 수가 이 표기다.</summary>
    template <typename T>
    [[nodiscard]] T ReadLittleEndian(const std::span<const std::byte> bytes, const std::size_t offset)
    {
        T value{};
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        return value;
    }

    [[nodiscard]] bool HasTag(
        const std::span<const std::byte> bytes, const std::size_t offset, const char (&tag)[5])
    {
        return std::memcmp(bytes.data() + offset, tag, 4) == 0;
    }
}

bool DecodeWavAudio(
    const std::span<const std::byte> fileBytes, DecodedWavAudio& decoded, std::string& error)
{
    // RIFF 머리: "RIFF" <크기> "WAVE". 이보다 짧은 파일은 WAV가 아니다.
    constexpr std::size_t RiffHeaderSize = 12;
    if (fileBytes.size() < RiffHeaderSize || !HasTag(fileBytes, 0, "RIFF") ||
        !HasTag(fileBytes, 8, "WAVE"))
    {
        error = "not a RIFF WAVE file";
        return false;
    }

    // 청크들을 걷는다. fmt가 포맷을, data가 샘플을 말한다. 그 밖의 청크 — LIST, cue 등 — 는
    // 건너뛴다.
    bool formatSeen = false;
    std::uint16_t formatTag = 0;
    std::uint16_t channelCount = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t byteRate = 0;
    std::uint16_t blockAlign = 0;
    std::uint16_t bitsPerSample = 0;
    std::span<const std::byte> sampleBytes;

    std::size_t offset = RiffHeaderSize;
    while (offset + 8 <= fileBytes.size())
    {
        const std::size_t chunkStart = offset + 8;
        const std::uint32_t chunkSize = ReadLittleEndian<std::uint32_t>(fileBytes, offset + 4);
        if (chunkStart + chunkSize > fileBytes.size())
        {
            error = "a chunk runs past the end of the file";
            return false;
        }

        if (HasTag(fileBytes, offset, "fmt "))
        {
            if (chunkSize < 16)
            {
                error = "the format chunk is too short";
                return false;
            }
            formatSeen = true;
            formatTag = ReadLittleEndian<std::uint16_t>(fileBytes, chunkStart);
            channelCount = ReadLittleEndian<std::uint16_t>(fileBytes, chunkStart + 2);
            sampleRate = ReadLittleEndian<std::uint32_t>(fileBytes, chunkStart + 4);
            byteRate = ReadLittleEndian<std::uint32_t>(fileBytes, chunkStart + 8);
            blockAlign = ReadLittleEndian<std::uint16_t>(fileBytes, chunkStart + 12);
            bitsPerSample = ReadLittleEndian<std::uint16_t>(fileBytes, chunkStart + 14);
        }
        else if (HasTag(fileBytes, offset, "data"))
        {
            sampleBytes = fileBytes.subspan(chunkStart, chunkSize);
        }

        // 청크는 2바이트 정렬로 놓인다: 홀수 크기 뒤에는 패딩 한 바이트가 온다.
        offset = chunkStart + chunkSize + (chunkSize % 2);
    }

    if (!formatSeen || sampleBytes.empty())
    {
        error = "the file has no format or no sample data";
        return false;
    }
    // 지원 경계는 좁고 명시적이다: 16/24비트 정수 PCM, 모노/스테레오. 그 밖은 어떤 파일인지
    // 말해 주고 거절한다 — 조용한 무음보다 이유 있는 거부가 낫다.
    if (formatTag != 1)
    {
        error = "only integer PCM is supported (format tag " + std::to_string(formatTag) + ")";
        return false;
    }
    if (bitsPerSample != 16 && bitsPerSample != 24)
    {
        error = "only 16-bit or 24-bit samples are supported (" + std::to_string(bitsPerSample) + "-bit)";
        return false;
    }
    if (channelCount != 1 && channelCount != 2)
    {
        error = "only mono or stereo is supported (" + std::to_string(channelCount) + " channels)";
        return false;
    }
    if (sampleRate == 0)
    {
        error = "the sample rate is zero";
        return false;
    }

    const std::size_t bytesPerSample = bitsPerSample / 8;
    const std::size_t expectedBlockAlign = channelCount * bytesPerSample;
    if (blockAlign != expectedBlockAlign ||
        static_cast<std::uint64_t>(byteRate) != static_cast<std::uint64_t>(sampleRate) * expectedBlockAlign)
    {
        error = "PCM block alignment or byte rate does not match the sample format";
        return false;
    }
    if (sampleBytes.size() % expectedBlockAlign != 0)
    {
        error = "the data chunk contains a truncated PCM sample frame";
        return false;
    }
    const std::size_t sampleCount = sampleBytes.size() / bytesPerSample;
    if (sampleCount == 0)
    {
        error = "the data chunk holds no samples";
        return false;
    }

    DecodedWavAudio result;
    result.channelCount = channelCount;
    result.sampleRate = sampleRate;
    result.samples.resize(sampleCount);
    if (bitsPerSample == 16)
    {
        std::memcpy(result.samples.data(), sampleBytes.data(), sampleCount * sizeof(std::int16_t));
    }
    else
    {
        // Signed little-endian PCM24 -> PCM16 keeps the most significant 16 bits. Dropping the
        // low byte preserves the sign and full-scale endpoints without overflow or normalization;
        // frame count, channel order and sample rate remain unchanged.
        for (std::size_t index = 0; index < sampleCount; ++index)
            result.samples[index] = ReadLittleEndian<std::int16_t>(sampleBytes, index * 3 + 1);
    }
    decoded = std::move(result);
    error.clear();
    return true;
}

}
