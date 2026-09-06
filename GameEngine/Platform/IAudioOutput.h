#pragma once

#include <cstdint>
#include <span>

namespace GameEngine::Platform
{

/// <summary>보이스 하나가 재생할 PCM 버퍼의 서술이다. 16비트 정수 샘플, 채널 인터리브다.</summary>
struct AudioVoiceDescription
{
    /// <summary>채널 수다. 1(모노) 또는 2(스테레오)다.</summary>
    std::uint32_t channelCount = 0;
    /// <summary>초당 샘플 프레임 수다. 예: 44100.</summary>
    std::uint32_t sampleRate = 0;
    /// <summary>
    /// 인터리브된 16비트 샘플들이다. 출력은 복사하지 않고 이 메모리에서 재생하므로, 호출자는
    /// 보이스를 파괴할 때까지 샘플을 살려 두어야 한다.
    /// </summary>
    std::span<const std::int16_t> samples;
    /// <summary>끝에 닿으면 처음부터 반복할지다.</summary>
    bool loop = false;
};

/// <summary>
/// 플랫폼 오디오 출력이다. 창이나 클립보드처럼 플랫폼 설비라서 인터페이스로 건넌다: 런타임의
/// 오디오 시스템이 소리를 내되 어느 오디오 API인지 몰라야 하고, 테스트는 출력 장치 없이 가짜를
/// 꽂는다.
///
/// 보이스는 버퍼 하나를 재생하는 단위다: 만들 때 버퍼가 제출되고, 시작/정지는 커서를 보존하는
/// 재개/일시정지다. 처음부터 다시 재생하는 것은 보이스를 파괴하고 다시 만드는 것이다 — 되감기
/// 상태 기계를 두는 대신 만들기를 값싸게 유지한다.
/// </summary>
class IAudioOutput
{
public:
    virtual ~IAudioOutput() = default;

    IAudioOutput(const IAudioOutput&) = delete;
    IAudioOutput& operator=(const IAudioOutput&) = delete;
    IAudioOutput(IAudioOutput&&) = delete;
    IAudioOutput& operator=(IAudioOutput&&) = delete;

    /// <summary>출력 장치를 연다. 장치가 없는 머신에서는 false다 — 무음 실행은 오류가 아니다.</summary>
    /// <returns>이후 보이스를 만들 수 있으면 true이다.</returns>
    [[nodiscard]] virtual bool Initialize() = 0;

    /// <summary>
    /// 열려 있던 출력이 되돌릴 수 없이 실패했는지다. 재생 중에 출력 장치가 사라지는 것이 그
    /// 경우다.
    ///
    /// 처음부터 장치가 없던 것과 달리 이쪽은 흔적을 남긴다: 이미 만들어진 보이스들이 진행을
    /// 멈춘 채 남아, 끝나지도 않고 소리도 내지 않는다. 그래서 오디오 시스템이 이것을 물어
    /// 장치 없는 머신과 같은 자리 — 상태만 있고 소리는 없는 — 로 강등한다.
    ///
    /// 기본값이 false인 이유는 대부분의 출력이 이런 실패를 겪을 수 없기 때문이다. 테스트의
    /// 가짜 출력이 그렇고, 그런 구현은 이 함수를 몰라도 된다.
    /// </summary>
    /// <returns>더는 소리를 낼 수 없으면 true이다.</returns>
    [[nodiscard]] virtual bool HasFailed() { return false; }

    /// <summary>버퍼를 재생할 보이스를 만든다. 만들어진 보이스는 아직 정지 상태다.</summary>
    /// <param name="description">재생할 PCM 버퍼다. 샘플 메모리는 보이스보다 오래 살아야 한다.</param>
    /// <returns>보이스 id이며, 만들지 못하면 0이다.</returns>
    [[nodiscard]] virtual std::uint64_t CreateVoice(const AudioVoiceDescription& description) = 0;

    /// <summary>보이스를 시작하거나 일시정지 지점에서 재개한다.</summary>
    /// <param name="voiceId">대상 보이스다.</param>
    virtual void StartVoice(std::uint64_t voiceId) = 0;

    /// <summary>보이스를 일시정지한다. 재생 커서는 남는다.</summary>
    /// <param name="voiceId">대상 보이스다.</param>
    virtual void StopVoice(std::uint64_t voiceId) = 0;

    /// <summary>보이스의 볼륨을 바꾼다.</summary>
    /// <param name="voiceId">대상 보이스다.</param>
    /// <param name="volume">0(무음)부터 1(원본 크기)까지다.</param>
    virtual void SetVoiceVolume(std::uint64_t voiceId, float volume) = 0;

    /// <summary>반복 없는 보이스가 버퍼 끝까지 재생을 마쳤는지다.</summary>
    /// <param name="voiceId">대상 보이스다.</param>
    /// <returns>더 재생할 것이 없으면 true이다. 반복 보이스는 언제나 false다.</returns>
    [[nodiscard]] virtual bool IsVoiceFinished(std::uint64_t voiceId) = 0;

    /// <summary>보이스를 멈추고 파괴한다. 이후에는 샘플 메모리를 놓아도 된다.</summary>
    /// <param name="voiceId">대상 보이스다.</param>
    virtual void DestroyVoice(std::uint64_t voiceId) = 0;

protected:
    IAudioOutput() = default;
};

}
