#include "pch.h"
#include "AudioSystem.h"

#include <cmath>
#include <utility>

#include "AudioListener.h"
#include "AudioSource.h"
#include "GameObject.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Transform.h"
#include "../Assets/AssetDatabase.h"
#include "../Diagnostics/Debug.h"

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// 리스너까지의 거리에 따른 선형 감쇠다. minDistance 안쪽은 1(감쇠 없음), maxDistance
    /// 밖은 0(무음), 그 사이는 선형 보간이다. `AudioSource::SetMinDistance`/`SetMaxDistance`가
    /// minDistance <= maxDistance를 지키므로 나눗셈은 이 범위 안에서만 일어난다.
    /// </summary>
    float ComputeDistanceAttenuation(
        const float distance, const float minDistance, const float maxDistance)
    {
        if (distance <= minDistance)
        {
            return 1.0f;
        }
        if (distance >= maxDistance)
        {
            return 0.0f;
        }
        return 1.0f - (distance - minDistance) / (maxDistance - minDistance);
    }
}

AudioSystem::AudioSystem(std::unique_ptr<Platform::IAudioOutput> output)
    : mOutput(std::move(output))
{
}

AudioSystem::~AudioSystem()
{
    // 샘플 메모리 계약을 지키는 순서다: 보이스를 먼저 전부 거두고, 그 뒤에야 클립 참조가
    // 풀린다. 멤버 파괴 순서에 맡기면 출력이 아직 재생 중인 메모리를 클립 해제가 먼저 거둘 수
    // 있다.
    for (auto& [sourceId, voice] : mVoices)
    {
        if (mOutputReady && voice.voiceId != 0)
        {
            mOutput->DestroyVoice(voice.voiceId);
        }
    }
    mVoices.clear();
}

bool AudioSystem::EnsureOutput()
{
    if (mOutputFailed || !mOutput)
    {
        return false;
    }
    if (mOutputReady)
    {
        // 열려 있던 출력도 사라질 수 있다: 재생 중에 출력 장치가 뽑히면 XAudio2가 치명 오류를
        // 보고하고 출력이 스스로 무너진다. 그때부터는 장치가 없던 머신과 같은 자리이므로, 처음
        // 열기에 실패한 것과 같은 기억에 합류시킨다 — 한 번 겪고, 이후에는 조용히 무음이다.
        if (mOutput->HasFailed())
        {
            mOutputReady = false;
            mOutputFailed = true;
            return false;
        }
        return true;
    }
    if (!mOutput->Initialize())
    {
        // 장치가 없는 머신이다. 실패는 한 번만 겪고, 이후에는 조용히 무음으로 남는다.
        mOutputFailed = true;
        return false;
    }
    mOutputReady = true;
    return true;
}

void AudioSystem::DestroyVoice(SourceVoice& voice)
{
    if (voice.voiceId != 0)
    {
        mOutput->DestroyVoice(voice.voiceId);
        voice.voiceId = 0;
    }
    voice.clip.reset();
}

void AudioSystem::Synchronize(
    SceneManager& sceneManager, const Assets::AssetDatabase& assetDatabase)
{
    for (auto& [sourceId, voice] : mVoices)
    {
        voice.seen = false;
    }

    // 승자 리스너를 먼저 정한다: 활성인 것 중 인스턴스 id가 가장 작은 것이다. 경고 로그는
    // 일부러 남기지 않는다 — 여러 리스너를 만드는 경우는 있을 수 있고, 이 규칙만으로 결정을
    // 예측 가능하게 하는 것으로 충분하다.
    const AudioListener* winningListener = nullptr;
    for (const auto& [sceneId, scene] : sceneManager.GetActiveScenes())
    {
        for (const auto& [objectId, gameObject] : scene->GetGameObjects())
        {
            if (!gameObject)
            {
                continue;
            }
            for (AudioListener* const listener : gameObject->GetComponents<AudioListener>())
            {
                if (!listener->IsActiveAndEnabled())
                {
                    continue;
                }
                if (!winningListener ||
                    listener->GetInstanceId() < winningListener->GetInstanceId())
                {
                    winningListener = listener;
                }
            }
        }
    }
    const std::optional<Math::Vector3> listenerPosition = winningListener
        ? std::make_optional(winningListener->GetGameObject()->GetTransform().GetWorldPosition())
        : std::nullopt;

    for (const auto& [sceneId, scene] : sceneManager.GetActiveScenes())
    {
        for (const auto& [objectId, gameObject] : scene->GetGameObjects())
        {
            if (!gameObject)
            {
                continue;
            }
            for (AudioSource* const source : gameObject->GetComponents<AudioSource>())
            {
                SynchronizeSource(*source, assetDatabase, listenerPosition);
            }
        }
    }

    // 이번 동기화가 만나지 못한 소스의 보이스를 거둔다: 파괴된 컴포넌트, 지워진 객체, 내려간
    // 장면이 전부 이 길로 조용해진다.
    std::erase_if(mVoices, [this](auto& entry)
    {
        if (entry.second.seen)
        {
            return false;
        }
        DestroyVoice(entry.second);
        return true;
    });
    std::erase_if(mReportedFailures, [this](const auto& entry)
    {
        return !mVoices.contains(entry.first);
    });
}

void AudioSystem::SynchronizeSource(
    AudioSource& source, const Assets::AssetDatabase& assetDatabase,
    const std::optional<Math::Vector3>& listenerPosition)
{
    const unsigned int sourceId = source.GetInstanceId();
    auto existing = mVoices.find(sourceId);
    if (existing != mVoices.end())
    {
        existing->second.seen = true;
        if (existing->second.clipRevision != source.mClipRevision)
        {
            DestroyVoice(existing->second);
            mVoices.erase(existing);
            existing = mVoices.end();
            mReportedFailures.erase(sourceId);
        }
    }

    // 들리는 조건: enabled이고 계층에서 활성이다. 조건을 잃으면 보이스는 파괴된다 — 상태가
    // Playing인 채 조건을 되찾으면 아래의 "보이스 없음 + Playing" 경로가 처음부터 다시 만든다.
    const GameObject* const owner = source.GetGameObject();
    const bool audible = source.IsEnabled() && owner && owner->IsActiveInHierarchy();
    const AudioSource::PlaybackState state = source.GetPlaybackState();

    if (state == AudioSource::PlaybackState::Stopped || !audible)
    {
        if (existing != mVoices.end())
        {
            DestroyVoice(existing->second);
            mVoices.erase(existing);
        }
        return;
    }

    // 번호가 바뀌었다면 무슨 전이였는지 직전 상태와의 쌍이 말한다. Play/Pause/Stop이 모두
    // 번호를 올리므로 번호 자체는 "무언가 일어났다"까지만 말한다.
    //   Playing → Paused : 일시정지. 커서를 지켜야 하므로 보이스는 그대로 둔다.
    //   Paused  → Playing: 재개. 마찬가지로 그대로 둔다.
    //   Playing → Playing: Play 재호출. 처음부터이므로 보이스를 새로 만든다.
    // 보이스가 없는 자리(로드 실패)는 지킬 커서가 없어 언제나 다시 만드는 쪽이다.
    if (existing != mVoices.end() &&
        existing->second.playbackRevision != source.GetPlaybackRevision())
    {
        const bool restart = existing->second.voiceId == 0 ||
            (state == AudioSource::PlaybackState::Playing &&
                existing->second.lastState != AudioSource::PlaybackState::Paused);
        if (restart)
        {
            DestroyVoice(existing->second);
            mVoices.erase(existing);
            existing = mVoices.end();
        }
        else
        {
            existing->second.playbackRevision = source.GetPlaybackRevision();
        }
    }

    if (existing == mVoices.end())
    {
        // 같은 재생의 실패를 프레임마다 반복해 겪지 않는다. 다음 Play가 번호를 올리면 다시
        // 시도한다.
        if (const auto reported = mReportedFailures.find(sourceId);
            reported != mReportedFailures.end() &&
            reported->second == source.GetPlaybackRevision())
        {
            return;
        }

        const std::shared_ptr<const Assets::AudioData> clip =
            assetDatabase.LoadAudioClip(source.GetClip());
        if (!clip || !clip->IsValid())
        {
            source.mClipDurationSeconds = 0.0;
            Diagnostics::Debug::LogError(
                "An AudioSource's clip cannot be played — its audio payload is unavailable. "
                "clip=", DescribeAssetReference(assetDatabase, source.GetClip()));
            mReportedFailures[sourceId] = source.GetPlaybackRevision();
            // 청소가 이 항목을 소스와 함께 거둘 수 있게 자리는 남긴다.
            mVoices.emplace(sourceId, SourceVoice{ .voiceId = 0,
                .playbackRevision = source.GetPlaybackRevision(), .clipRevision = source.mClipRevision,
                .lastState = state,
                .clip = nullptr, .volume = -1.0f, .paused = false, .loop = false, .seen = true });
            return;
        }
        source.mClipDurationSeconds = clip->GetDurationSeconds();
        if (!EnsureOutput())
        {
            // 출력이 없다: 상태는 그대로 두고 소리만 없다. 진행도 없으므로 끝남 판정도 없다.
            // 길이는 디코딩만으로 정해진다. 무음 자리도 기억해 같은 클립을 매 프레임 읽지 않는다.
            mVoices.emplace(sourceId, SourceVoice{ .voiceId = 0,
                .playbackRevision = source.GetPlaybackRevision(), .clipRevision = source.mClipRevision,
                .lastState = state, .clip = clip, .volume = -1.0f, .paused = false,
                .loop = source.IsLooping(), .seen = true });
            return;
        }

        Platform::AudioVoiceDescription description;
        description.channelCount = clip->channelCount;
        description.sampleRate = clip->sampleRate;
        description.samples = clip->samples;
        description.loop = source.IsLooping();
        const std::uint64_t voiceId = mOutput->CreateVoice(description);
        if (voiceId == 0)
        {
            mReportedFailures[sourceId] = source.GetPlaybackRevision();
            mVoices.emplace(sourceId, SourceVoice{ .voiceId = 0,
                .playbackRevision = source.GetPlaybackRevision(), .clipRevision = source.mClipRevision,
                .lastState = state,
                .clip = nullptr, .volume = -1.0f, .paused = false, .loop = false, .seen = true });
            return;
        }
        existing = mVoices.emplace(sourceId, SourceVoice{ .voiceId = voiceId,
            .playbackRevision = source.GetPlaybackRevision(), .clipRevision = source.mClipRevision,
            .lastState = state, .clip = clip,
            .volume = -1.0f, .paused = true, .loop = source.IsLooping(), .seen = true }).first;
    }

    SourceVoice& voice = existing->second;
    voice.seen = true;
    if (voice.voiceId == 0)
    {
        // 클립/보이스 로드 실패 또는 출력 없는 자리다. 번호가 그대로면 할 일이 없다.
        voice.lastState = state;
        return;
    }

    // 활성 리스너가 없으면 spatialBlend와 무관하게 무음이다. 보이스 자체는 계속 진행하므로
    // 리스너가 돌아와도 커서를 다시 시작하지 않고 그 시점의 소리가 이어진다.
    float effectiveVolume = listenerPosition ? source.GetVolume() : 0.0f;
    if (listenerPosition && source.GetSpatialBlend() > 0.0f)
    {
        const float distance =
            owner->GetTransform().GetWorldPosition().DistanceTo(*listenerPosition);
        const float attenuation = ComputeDistanceAttenuation(
            distance, source.GetMinDistance(), source.GetMaxDistance());
        effectiveVolume *= std::lerp(1.0f, attenuation, source.GetSpatialBlend());
    }
    if (voice.volume != effectiveVolume)
    {
        voice.volume = effectiveVolume;
        mOutput->SetVoiceVolume(voice.voiceId, voice.volume);
    }

    if (state == AudioSource::PlaybackState::Playing)
    {
        if (voice.paused)
        {
            mOutput->StartVoice(voice.voiceId);
            voice.paused = false;
        }
        // 반복 없는 재생이 끝에 닿았다: 보이스를 거두고 소스를 Stopped로 되돌린다 — Unity의
        // isPlaying이 꺼지는 것과 같은 관찰이다.
        if (!voice.loop && mOutput->IsVoiceFinished(voice.voiceId))
        {
            DestroyVoice(voice);
            mVoices.erase(existing);
            source.Stop();
            return;
        }
    }
    else if (state == AudioSource::PlaybackState::Paused && !voice.paused)
    {
        mOutput->StopVoice(voice.voiceId);
        voice.paused = true;
    }
    voice.lastState = state;
}

}
