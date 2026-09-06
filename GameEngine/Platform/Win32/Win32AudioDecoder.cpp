#include "pch.h"
#include "Win32AudioDecoder.h"

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <sstream>
#include <string>
#include <utility>

#include "Win32ComApartment.h"
#include "../../Diagnostics/ApiFailure.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")

namespace GameEngine::Platform::Win32
{

namespace
{
    using Microsoft::WRL::ComPtr;
    constexpr DWORD AudioStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

    bool Check(const HRESULT result, const char* const operation, std::string& error)
    {
        if (SUCCEEDED(result)) return true;
        std::ostringstream message;
        message << operation << " failed (HRESULT 0x" << std::hex <<
            static_cast<unsigned long>(result) << ")";
        error = message.str();
        return false;
    }

    /// <summary>이 호출의 모든 MF 객체보다 먼저 만들고 나중에 닫는 플랫폼 참여다.</summary>
    class MediaFoundationScope final
    {
    public:
        MediaFoundationScope() : mResult(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET)) {}
        ~MediaFoundationScope()
        {
            if (SUCCEEDED(mResult))
            {
                const HRESULT result = MFShutdown();
                if (FAILED(result))
                {
                    Diagnostics::LogApiFailure("Shutting down the audio decoder",
                        "Media Foundation", static_cast<std::uint64_t>(
                            static_cast<unsigned long>(result)));
                }
            }
        }
        MediaFoundationScope(const MediaFoundationScope&) = delete;
        MediaFoundationScope& operator=(const MediaFoundationScope&) = delete;
        [[nodiscard]] HRESULT GetResult() const { return mResult; }

    private:
        HRESULT mResult;
    };

    bool ReadPcmFormat(IMFSourceReader& reader, DecodedAudio& decoded, std::string& error)
    {
        ComPtr<IMFMediaType> type;
        if (!Check(reader.GetCurrentMediaType(AudioStream, type.GetAddressOf()),
            "Reading the decoded audio format", error)) return false;

        GUID major{};
        GUID subtype{};
        UINT32 channels = 0;
        UINT32 rate = 0;
        UINT32 bits = 0;
        UINT32 alignment = 0;
        if (!Check(type->GetGUID(MF_MT_MAJOR_TYPE, &major), "Reading audio major type", error) ||
            !Check(type->GetGUID(MF_MT_SUBTYPE, &subtype), "Reading PCM subtype", error) ||
            !Check(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels), "Reading channel count", error) ||
            !Check(type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate), "Reading sample rate", error) ||
            !Check(type->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits), "Reading sample depth", error) ||
            !Check(type->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &alignment), "Reading PCM alignment", error))
        {
            return false;
        }
        if (major != MFMediaType_Audio || subtype != MFAudioFormat_PCM || bits != 16 ||
            (channels != 1 && channels != 2) || rate == 0 || alignment != channels * 2)
        {
            error = "the decoder did not produce aligned mono/stereo 16-bit PCM";
            return false;
        }
        if (decoded.channelCount != 0 &&
            (decoded.channelCount != channels || decoded.sampleRate != rate))
        {
            error = "the audio format changed during decoding";
            return false;
        }
        decoded.channelCount = channels;
        decoded.sampleRate = rate;
        return true;
    }

    bool DecodeMp3(const std::span<const std::byte> encodedBytes,
        const AudioDecodeLimits& limits, DecodedAudio& decoded, std::string& error)
    {
        // COM participation belongs to this thread and call, not to a decoder cached by an
        // importer that may later be used on a different thread.
        ComApartment apartment;
        if (!apartment.Initialize())
        {
            error = "COM initialization failed for audio decoding";
            return false;
        }
        MediaFoundationScope foundation;
        if (!Check(foundation.GetResult(), "Starting Media Foundation", error)) return false;

        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(encodedBytes.data()),
            static_cast<UINT>(encodedBytes.size())));
        if (!stream)
        {
            error = "allocating the encoded audio memory stream failed";
            return false;
        }
        ComPtr<IMFByteStream> byteStream;
        if (!Check(MFCreateMFByteStreamOnStream(stream.Get(), byteStream.GetAddressOf()),
            "Wrapping the audio memory stream", error)) return false;
        ComPtr<IMFAttributes> streamAttributes;
        if (!Check(byteStream.As(&streamAttributes), "Querying audio stream attributes", error) ||
            !Check(streamAttributes->SetString(MF_BYTESTREAM_CONTENT_TYPE, L"audio/mpeg"),
                "Identifying MP3 stream content", error)) return false;

        ComPtr<IMFSourceReader> reader;
        if (!Check(MFCreateSourceReaderFromByteStream(byteStream.Get(), nullptr, reader.GetAddressOf()),
            "Opening the encoded audio stream", error)) return false;
        ComPtr<IMFMediaType> nativeType;
        GUID nativeSubtype{};
        if (!Check(reader->GetNativeMediaType(AudioStream, 0, nativeType.GetAddressOf()),
                "Reading the encoded audio format", error) ||
            !Check(nativeType->GetGUID(MF_MT_SUBTYPE, &nativeSubtype),
                "Reading the encoded audio subtype", error)) return false;
        if (nativeSubtype != MFAudioFormat_MP3)
        {
            error = "the compressed audio decoder currently supports MP3 only";
            return false;
        }

        ComPtr<IMFMediaType> requestedType;
        if (!Check(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE),
                "Deselecting unused streams", error) ||
            !Check(reader->SetStreamSelection(AudioStream, TRUE), "Selecting the audio stream", error) ||
            !Check(MFCreateMediaType(requestedType.GetAddressOf()), "Creating PCM media type", error) ||
            !Check(requestedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio),
                "Requesting audio output", error) ||
            !Check(requestedType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "Requesting PCM output", error) ||
            !Check(requestedType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16),
                "Requesting 16-bit samples", error) ||
            !Check(reader->SetCurrentMediaType(AudioStream, nullptr, requestedType.Get()),
                "Configuring the MP3 decoder", error) ||
            !ReadPcmFormat(*reader.Get(), decoded, error)) return false;

        unsigned int emptySamples = 0;
        for (;;)
        {
            ComPtr<IMFSample> sample;
            DWORD flags = 0;
            if (!Check(reader->ReadSample(AudioStream, 0, nullptr, &flags, nullptr, sample.GetAddressOf()),
                "Decoding an MP3 sample", error)) return false;
            if ((flags & MF_SOURCE_READERF_ERROR) != 0)
            {
                error = "the MP3 source reader reported a decoding error";
                return false;
            }
            if ((flags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED |
                MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) != 0 &&
                !ReadPcmFormat(*reader.Get(), decoded, error)) return false;

            DWORD byteCount = 0;
            if (sample)
            {
                ComPtr<IMFMediaBuffer> buffer;
                if (!Check(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()),
                        "Reading PCM sample buffer", error) ||
                    !Check(buffer->GetCurrentLength(&byteCount), "Reading PCM buffer length", error))
                    return false;
                if (byteCount % (decoded.channelCount * sizeof(std::int16_t)) != 0)
                {
                    error = "the decoder produced a partial PCM sample frame";
                    return false;
                }
                const std::size_t existingBytes = decoded.samples.size() * sizeof(std::int16_t);
                // 압축 파일 크기는 PCM 크기를 제한하지 못한다. 각 청크를 붙이기 전에 예산을 확인한다.
                if (existingBytes > limits.maximumDecodedBytes ||
                    byteCount > limits.maximumDecodedBytes - existingBytes)
                {
                    error = "decoded audio exceeds the PCM byte limit";
                    return false;
                }
                if (byteCount != 0)
                {
                    const std::size_t firstSample = decoded.samples.size();
                    // Allocate before Lock: memcpy and Unlock below cannot throw, so even a
                    // failed allocation cannot leave a Media Foundation buffer locked.
                    decoded.samples.resize(firstSample + byteCount / sizeof(std::int16_t));
                    BYTE* bytes = nullptr;
                    DWORD lockedBytes = 0;
                    if (!Check(buffer->Lock(&bytes, nullptr, &lockedBytes), "Locking PCM data", error))
                        return false;
                    const bool matches = bytes && lockedBytes == byteCount;
                    if (matches) std::memcpy(decoded.samples.data() + firstSample, bytes, byteCount);
                    if (!Check(buffer->Unlock(), "Unlocking PCM data", error)) return false;
                    if (!matches)
                    {
                        error = "the PCM buffer changed while being read";
                        return false;
                    }
                }
            }
            // 마지막 읽기가 샘플과 EOF를 함께 줄 수 있으므로 샘플을 복사한 뒤 종료한다.
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) break;
            // PCM 바이트 예산으로 막을 수 없는 무진행 디코더도 유한 횟수 안에 실패시킨다.
            emptySamples = byteCount != 0 ? 0 : emptySamples + 1;
            if (emptySamples > 1024)
            {
                error = "the audio decoder stopped producing PCM samples";
                return false;
            }
        }
        if (!decoded.IsValid())
        {
            error = "the MP3 stream contains no playable PCM samples";
            return false;
        }
        return true;
    }
}

bool Win32AudioDecoder::Decode(const std::span<const std::byte> encodedBytes,
    const AudioDecodeLimits& limits, DecodedAudio& audio, std::string& error)
{
    if (encodedBytes.empty() || encodedBytes.size() > (std::numeric_limits<UINT>::max)())
    {
        error = "encoded audio is empty or too large for the memory stream";
        return false;
    }
    if (limits.maximumDecodedBytes < sizeof(std::int16_t))
    {
        error = "the PCM byte limit cannot hold one sample";
        return false;
    }
    try
    {
        DecodedAudio decoded;
        if (!DecodeMp3(encodedBytes, limits, decoded, error)) return false;
        audio = std::move(decoded);
        error.clear();
        return true;
    }
    catch (const std::bad_alloc&)
    {
        error = "allocating decoded audio failed";
        return false;
    }
}

}
