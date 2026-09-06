#pragma once

#include <cstdint>
#include <memory>

#include "../IAudioOutput.h"

namespace GameEngine::Platform::Win32
{

/// <summary>XAudio2로 소리를 낸다. 보이스 하나가 XAudio2 소스 보이스 하나다.</summary>
class Win32AudioOutput final : public IAudioOutput
{
public:
    Win32AudioOutput();
    ~Win32AudioOutput() override;

    [[nodiscard]] bool Initialize() override;
    [[nodiscard]] bool HasFailed() override;
    [[nodiscard]] std::uint64_t CreateVoice(const AudioVoiceDescription& description) override;
    void StartVoice(std::uint64_t voiceId) override;
    void StopVoice(std::uint64_t voiceId) override;
    void SetVoiceVolume(std::uint64_t voiceId, float volume) override;
    [[nodiscard]] bool IsVoiceFinished(std::uint64_t voiceId) override;
    void DestroyVoice(std::uint64_t voiceId) override;

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

}
