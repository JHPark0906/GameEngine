#include "pch.h"
#include "Win32AudioOutput.h"

#include <atomic>
#include <cstdint>
#include <ios>
#include <memory>
#include <unordered_map>
#include <utility>

#include <wrl/client.h>
#include <xaudio2.h>

#include "Win32ComApartment.h"
#include "../../Diagnostics/Debug.h"

#pragma comment(lib, "xaudio2.lib")

namespace GameEngine::Platform::Win32
{

/// <summary>
/// XAudio2 상태와 치명 오류를 전달받는 콜백이다.
///
/// 출력 장치를 잃으면 보이스와 버퍼 큐가 진행을 멈춰 재생 완료 판정만으로는 실패를 알 수 없다.
/// 콜백이 실패를 기록해 오디오 시스템이 이를 진단하고 재생 상태를 정리할 수 있게 한다.
/// </summary>
struct Win32AudioOutput::Implementation final : public IXAudio2EngineCallback
{
    ComApartment comApartment;
    Microsoft::WRL::ComPtr<IXAudio2> engine;
    /// <summary>모든 소스 보이스가 섞여 나가는 출력 끝단이다. engine이 소유한다.</summary>
    IXAudio2MasteringVoice* masteringVoice = nullptr;

    struct Voice
    {
        IXAudio2SourceVoice* sourceVoice = nullptr;
        bool loop = false;
    };
    std::unordered_map<std::uint64_t, Voice> voices;
    std::uint64_t nextVoiceId = 1;

    /// <summary>콜백 등록에 성공했는지다. 엔진을 놓기 전에 해제해야 한다.</summary>
    bool callbacksRegistered = false;
    /// <summary>
    /// 치명 오류가 보고되었는지다. XAudio2의 오디오 스레드가 쓰고 주 스레드가 읽으므로
    /// atomic이다.
    /// </summary>
    std::atomic<bool> criticalError{ false };
    /// <summary>보고된 HRESULT다. 로그 한 줄에 실린다.</summary>
    std::atomic<long> criticalErrorCode{ 0 };
    /// <summary>그 사실을 이미 말했는지다. 주 스레드만 만진다.</summary>
    bool reportedCriticalError = false;

    ~Implementation()
    {
        DestroyEngine();
    }

    // ---- IXAudio2EngineCallback. XAudio2의 오디오 스레드에서 불린다.

    void STDMETHODCALLTYPE OnProcessingPassStart() override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() override {}

    void STDMETHODCALLTYPE OnCriticalError(const HRESULT error) override
    {
        // 이 콜백 안에서 XAudio2를 다시 부르는 것은 금지다 — 오디오 스레드가 자기 자신을
        // 기다리게 된다. 그래서 여기서는 사실만 남기고, 엔진을 무너뜨리는 일도 로그도 주
        // 스레드가 ObserveCriticalError에서 한다.
        criticalErrorCode.store(static_cast<long>(error), std::memory_order_relaxed);
        criticalError.store(true, std::memory_order_release);
    }

    /// <summary>
    /// 치명 오류가 보고되어 있으면 엔진을 무너뜨리고 한 번만 말한다. 주 스레드에서만 부른다.
    /// </summary>
    /// <returns>이 출력이 되돌릴 수 없이 실패했으면 true이다.</returns>
    [[nodiscard]] bool ObserveCriticalError()
    {
        if (!criticalError.load(std::memory_order_acquire))
        {
            return false;
        }
        if (!reportedCriticalError)
        {
            reportedCriticalError = true;
            Diagnostics::Debug::LogWarning(
                "XAudio2 reported a critical error — the output device is gone. Audio stays "
                "silent for the rest of this run. code=0x", std::hex,
                static_cast<unsigned int>(criticalErrorCode.load(std::memory_order_relaxed)));
            DestroyEngine();
        }
        return true;
    }

    /// <summary>보이스와 엔진을 순서대로 놓는다. 두 번 불러도 안전하다.</summary>
    void DestroyEngine()
    {
        // 소스 보이스는 엔진보다 먼저 죽어야 한다. 엔진 파괴는 남은 보이스를 무효화한다.
        for (auto& [voiceId, voice] : voices)
        {
            if (voice.sourceVoice)
            {
                voice.sourceVoice->DestroyVoice();
            }
        }
        voices.clear();
        if (masteringVoice)
        {
            masteringVoice->DestroyVoice();
            masteringVoice = nullptr;
        }
        if (engine && callbacksRegistered)
        {
            engine->UnregisterForCallbacks(this);
            callbacksRegistered = false;
        }
        engine.Reset();
    }
};

Win32AudioOutput::Win32AudioOutput()
    : mImplementation(std::make_unique<Implementation>())
{
}

Win32AudioOutput::~Win32AudioOutput() = default;

bool Win32AudioOutput::Initialize()
{
    if (mImplementation->engine)
    {
        return true;
    }
    if (mImplementation->ObserveCriticalError())
    {
        // 한 번 무너진 출력은 다시 열지 않는다. 장치가 돌아왔는지는 여기서 알 수 없고, 매
        // 프레임 다시 여는 시도는 실패를 반복해 겪는 것뿐이다.
        return false;
    }
    if (!mImplementation->comApartment.Initialize())
    {
        Diagnostics::Debug::LogWarning("XAudio2 needs COM, which failed to initialize.");
        return false;
    }
    if (FAILED(XAudio2Create(mImplementation->engine.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR)))
    {
        Diagnostics::Debug::LogWarning("XAudio2 could not be created; audio stays silent.");
        mImplementation->engine.Reset();
        return false;
    }
    // 마스터링 보이스가 실제 출력 장치를 연다. 장치가 없는 머신 — CI, 원격 세션 — 은 여기서
    // 실패하고, 그것은 오류가 아니라 무음 실행이다.
    if (FAILED(mImplementation->engine->CreateMasteringVoice(&mImplementation->masteringVoice)))
    {
        Diagnostics::Debug::LogWarning("No audio output device is available; audio stays silent.");
        mImplementation->masteringVoice = nullptr;
        mImplementation->engine.Reset();
        return false;
    }
    // 장치 분리는 여기로만 도착한다. 등록에 실패해도 소리는 나므로 무음으로 강등하지 않지만,
    // 그때는 장치를 잃어도 아무도 말해 주지 않는다는 사실을 미리 적어 둔다.
    if (SUCCEEDED(mImplementation->engine->RegisterForCallbacks(mImplementation.get())))
    {
        mImplementation->callbacksRegistered = true;
    }
    else
    {
        Diagnostics::Debug::LogWarning(
            "XAudio2 engine callbacks could not be registered; a lost output device will go "
            "unreported.");
    }
    return true;
}

bool Win32AudioOutput::HasFailed()
{
    return mImplementation->ObserveCriticalError();
}

std::uint64_t Win32AudioOutput::CreateVoice(const AudioVoiceDescription& description)
{
    if (!mImplementation->engine || !mImplementation->masteringVoice ||
        description.channelCount == 0 || description.channelCount > 2 ||
        description.sampleRate == 0 || description.samples.empty())
    {
        return 0;
    }

    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(description.channelCount);
    format.nSamplesPerSec = description.sampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(description.channelCount * sizeof(std::int16_t));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    IXAudio2SourceVoice* sourceVoice = nullptr;
    if (FAILED(mImplementation->engine->CreateSourceVoice(&sourceVoice, &format)))
    {
        Diagnostics::Debug::LogWarning("An XAudio2 source voice could not be created.");
        return 0;
    }

    // 버퍼는 복사되지 않는다: 호출자가 보이스를 파괴할 때까지 샘플을 쥔다는 것이 인터페이스의
    // 계약이다.
    XAUDIO2_BUFFER buffer = {};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = static_cast<UINT32>(description.samples.size_bytes());
    buffer.pAudioData = reinterpret_cast<const BYTE*>(description.samples.data());
    buffer.LoopCount = description.loop ? XAUDIO2_LOOP_INFINITE : 0;
    if (FAILED(sourceVoice->SubmitSourceBuffer(&buffer)))
    {
        sourceVoice->DestroyVoice();
        Diagnostics::Debug::LogWarning("An audio buffer could not be submitted to XAudio2.");
        return 0;
    }

    const std::uint64_t voiceId = mImplementation->nextVoiceId++;
    mImplementation->voices.emplace(voiceId, Implementation::Voice{ sourceVoice, description.loop });
    return voiceId;
}

void Win32AudioOutput::StartVoice(const std::uint64_t voiceId)
{
    const auto voice = mImplementation->voices.find(voiceId);
    if (voice != mImplementation->voices.end())
    {
        static_cast<void>(voice->second.sourceVoice->Start(0));
    }
}

void Win32AudioOutput::StopVoice(const std::uint64_t voiceId)
{
    const auto voice = mImplementation->voices.find(voiceId);
    if (voice != mImplementation->voices.end())
    {
        // Stop은 커서를 남긴다 — 일시정지다. 처음부터가 필요한 쪽은 파괴하고 다시 만든다.
        static_cast<void>(voice->second.sourceVoice->Stop(0));
    }
}

void Win32AudioOutput::SetVoiceVolume(const std::uint64_t voiceId, const float volume)
{
    const auto voice = mImplementation->voices.find(voiceId);
    if (voice != mImplementation->voices.end())
    {
        static_cast<void>(voice->second.sourceVoice->SetVolume(volume));
    }
}

bool Win32AudioOutput::IsVoiceFinished(const std::uint64_t voiceId)
{
    if (mImplementation->ObserveCriticalError())
    {
        // 장치가 사라진 보이스는 큐를 비우지 못한 채 멈춰 있다. 그것을 "아직 안 끝났다"고
        // 계속 말하면 반복 없는 소스가 영원히 Playing으로 남는다 — 끝났다고 말해 소스가
        // Stopped로 돌아가게 한다.
        return true;
    }
    const auto voice = mImplementation->voices.find(voiceId);
    if (voice == mImplementation->voices.end() || voice->second.loop)
    {
        return false;
    }
    XAUDIO2_VOICE_STATE state = {};
    voice->second.sourceVoice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return state.BuffersQueued == 0;
}

void Win32AudioOutput::DestroyVoice(const std::uint64_t voiceId)
{
    const auto voice = mImplementation->voices.find(voiceId);
    if (voice == mImplementation->voices.end())
    {
        return;
    }
    static_cast<void>(voice->second.sourceVoice->Stop(0));
    voice->second.sourceVoice->DestroyVoice();
    mImplementation->voices.erase(voice);
}

}
