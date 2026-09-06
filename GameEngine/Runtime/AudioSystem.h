#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

#include "../Assets/AudioData.h"
#include "../Math/Vector.h"
#include "AudioSource.h"
#include "../Platform/IAudioOutput.h"

namespace GameEngine::Assets
{
class AssetDatabase;
}

namespace GameEngine::Runtime
{

class SceneManager;

/// <summary>
/// AudioSource들의 재생 상태를 플랫폼 보이스에 맞추는 시스템이다. 렌더 프론트엔드가 렌더러
/// 컴포넌트를 읽어 프레임을 만들 듯, 이 시스템은 프레임마다 장면의 소스들을 읽어 소리를 만든다
/// — 컴포넌트는 상태만 쥐고, 플랫폼을 아는 쪽은 여기 하나다.
///
/// 수명 규칙:
/// - 소스는 enabled이고 계층에서 활성인 동안만 들린다. 조건을 잃으면 보이스는 파괴되고, 상태가
///   Playing인 채 조건을 되찾으면 처음부터 다시 재생된다 — 일시정지가 아니라 정지다.
/// - 재생 중의 Play 재호출은 처음부터 다시 시작한다. 일시정지 중의 Play는 이어서 재개한다:
///   `AudioSource`에 UnPause가 따로 없으므로 Play가 그 역할을 겸하고, 그래야 Pause가 커서를
///   지키는 일시정지로 남는다. 두 경우 다 재생 번호(revision)는 올라가므로 번호만으로는 구별할
///   수 없다 — 시스템이 직전 상태를 함께 기억해 전이를 읽는 이유다.
/// - 반복 없는 보이스가 끝에 닿으면 소스는 Stopped가 된다.
/// - 장면이 내려가거나 객체·컴포넌트가 파괴되면 다음 동기화의 청소가 보이스를 거둔다. Game이
///   장면을 내린 직후 한 번 더 동기화를 부르는 이유다 — 업데이트가 더는 돌지 않는 에디터의
///   Play 이탈에서도 소리가 즉시 멎는다.
/// - 출력 장치가 없는 머신에서는 소리 없이 상태만 남는다. 무음 실행은 오류가 아니다.
/// - loop 플래그는 재생이 시작되는 시점에 읽힌다. 재생 중의 변경은 다음 시작부터 적용된다.
/// - 활성 `AudioListener`가 없으면 spatialBlend와 무관하게 모든 보이스가 무음이다. 재생은
///   계속 진행하고 끝남도 판정하므로, 리스너를 켜도 처음부터 재생하지 않는다.
/// - `AudioListener`가 있으면 그 월드 위치와 소스의 거리가 spatialBlend만큼 볼륨을 깎는다 —
///   minDistance 안쪽은 감쇠 없음, maxDistance 밖은 무음, 그 사이는 선형 보간이다. 활성
///   리스너가 둘 이상이면 인스턴스 id가 가장 작은 것이 이긴다. spatialBlend가 0이면 리스너와의
///   거리는 무시하고 소스의 volume을 쓴다.
/// </summary>
class AudioSystem final
{
public:
    /// <summary>플랫폼 출력을 받는다. 출력 초기화는 첫 보이스가 필요할 때까지 미뤄진다 —
    /// 소리 낼 일 없는 런타임(에디터 UI)이 오디오 장치를 열지 않게.</summary>
    /// <param name="output">플랫폼 오디오 출력이다. null이면 시스템은 무음으로 남는다.</param>
    explicit AudioSystem(std::unique_ptr<Platform::IAudioOutput> output);
    ~AudioSystem();

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;
    AudioSystem(AudioSystem&&) = delete;
    AudioSystem& operator=(AudioSystem&&) = delete;

    /// <summary>
    /// 활성 장면들의 AudioSource 상태를 보이스에 맞춘다. 시작·정지·볼륨을 반영하고, 사라진
    /// 소스의 보이스를 거두고, 끝난 보이스의 소스를 Stopped로 되돌린다. 프레임마다 한 번, 그리고
    /// 장면이 내려간 직후에 불린다.
    /// </summary>
    /// <param name="sceneManager">활성 장면들이다.</param>
    /// <param name="assetDatabase">클립 페이로드를 로드할 데이터베이스다.</param>
    void Synchronize(SceneManager& sceneManager, const Assets::AssetDatabase& assetDatabase);

private:
    /// <summary>소스 하나에 붙어 있는 보이스와, 그 보이스가 어떤 재생이었는지다.</summary>
    struct SourceVoice
    {
        std::uint64_t voiceId = 0;
        /// <summary>이 보이스에 대해 마지막으로 관찰한 재생 번호다.</summary>
        unsigned long long playbackRevision = 0;
        /// <summary>클립 교체는 일시정지 중에도 이전 보이스를 버리게 한다.</summary>
        unsigned long long clipRevision = 0;
        /// <summary>그 번호를 관찰했을 때의 재생 상태다. 번호가 바뀐 이유 — 재시작인지 재개인지 —
        /// 를 읽는 것이 이 값과 새 상태의 쌍이다.</summary>
        AudioSource::PlaybackState lastState = AudioSource::PlaybackState::Stopped;
        /// <summary>보이스가 사는 동안 샘플 메모리를 지키는 참조다. 출력은 복사하지 않는다.</summary>
        std::shared_ptr<const Assets::AudioData> clip;
        float volume = -1.0f;
        bool paused = false;
        bool loop = false;
        /// <summary>이번 동기화에서 소스를 다시 만났는지다. 못 만난 보이스는 청소된다.</summary>
        bool seen = false;
    };

    void SynchronizeSource(
        AudioSource& source, const Assets::AssetDatabase& assetDatabase,
        const std::optional<Math::Vector3>& listenerPosition);
    [[nodiscard]] bool EnsureOutput();
    void DestroyVoice(SourceVoice& voice);

    std::unique_ptr<Platform::IAudioOutput> mOutput;
    bool mOutputReady = false;
    bool mOutputFailed = false;
    /// <summary>AudioSource 인스턴스 id → 보이스.</summary>
    std::unordered_map<unsigned int, SourceVoice> mVoices;
    /// <summary>클립 로드 실패를 이미 로그로 말한 (소스 id, 재생 번호)다. 같은 실패를 프레임마다
    /// 반복해 말하지 않는다.</summary>
    std::unordered_map<unsigned int, unsigned long long> mReportedFailures;
};

}
