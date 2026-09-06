#pragma once

#include "../Assets/AssetReference.h"
#include "Behaviour.h"

#include <memory>
#include <vector>

namespace GameEngine::Runtime
{

/// <summary>오디오 클립 설정과 재생 상태를 소유하는 컴포넌트이다.</summary>
/// <remarks>
/// 실제 소리는 런타임의 `AudioSystem`이 프레임마다 이 상태를 플랫폼 보이스에 맞추는 것으로
/// 난다 — 렌더러 컴포넌트가 상태만 쥐고 프론트엔드가 그리는 것과 같은 분업이다. volume과
/// loop, playOnAwake는 출력에 반영된다. minDistance와 maxDistance는 장면의 `AudioListener`와의
/// 거리에 따라 spatialBlend가 섞는 감쇠의 경계로 적용된다. 활성 AudioListener가 없으면
/// spatialBlend가 0이어도 무음이며, 재생 시간은 계속 진행한다. pitch는 값만 보존되고 아직 출력에
/// 적용되지 않는다.
/// </remarks>
class AudioSource final : public Behaviour
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }
    enum class PlaybackState
    {
        Stopped,
        Playing,
        Paused
    };

    [[nodiscard]] const Assets::AssetReference& GetClip() const { return mClip; }
    void SetClip(Assets::AssetReference clip);

    /// <summary>
    /// 디코딩된 클립의 원래 길이(초)다. 첫 활성 재생 동기화 전이나 로드 실패 때는 0이다.
    /// 출력 장치가 없어도 알 수 있고, Stop 후에도 유지되며, 클립이 바뀌면 즉시 0으로 돌아간다.
    /// 반복과 pitch는 이 원본 길이를 바꾸지 않는다.
    /// </summary>
    [[nodiscard]] double GetClipDurationSeconds() const { return mClipDurationSeconds; }

    /// <summary>
    /// 클립을 에셋 참조로 보고한다. 다른 렌더러와 마찬가지로 프리로드와 언로드 스윕이
    /// 이 참조를 사용하여 클립의 상주를 관리한다.
    /// </summary>
    void CollectAssetReferences(std::vector<Assets::AssetReference>& references) const override
    {
        if (mClip.IsValid())
        {
            references.push_back(mClip);
        }
    }

    [[nodiscard]] float GetVolume() const { return mVolume; }
    void SetVolume(float volume);
    [[nodiscard]] float GetPitch() const { return mPitch; }
    void SetPitch(float pitch);
    [[nodiscard]] float GetSpatialBlend() const { return mSpatialBlend; }
    void SetSpatialBlend(float spatialBlend);
    [[nodiscard]] float GetMinDistance() const { return mMinDistance; }
    void SetMinDistance(float minDistance);
    [[nodiscard]] float GetMaxDistance() const { return mMaxDistance; }
    void SetMaxDistance(float maxDistance);

    [[nodiscard]] bool IsLooping() const { return mLoop; }
    void SetLooping(bool loop) { mLoop = loop; }
    [[nodiscard]] bool GetPlayOnAwake() const { return mPlayOnAwake; }
    void SetPlayOnAwake(bool playOnAwake) { mPlayOnAwake = playOnAwake; }

    [[nodiscard]] PlaybackState GetPlaybackState() const { return mPlaybackState; }
    [[nodiscard]] bool IsPlaying() const { return mPlaybackState == PlaybackState::Playing; }
    [[nodiscard]] unsigned long long GetPlaybackRevision() const { return mPlaybackRevision; }

    // true는 재생 요청을 수락했다는 뜻이다. 클립 로드와 출력 장치 확인은 AudioSystem 동기화 때 한다.
    // Playing에서 Play하면 처음부터, Paused에서 Play하면 기존 보이스의 커서부터 재개한다.
    bool Play();
    bool Pause();
    void Stop();

private:
    friend class AudioSystem;

    void OnAttached() override;
    void OnRemoved() override;
    void OnPropertiesRestored() noexcept override;

    Assets::AssetReference mClip;
    double mClipDurationSeconds = 0.0;
    unsigned long long mClipRevision = 0;
    PlaybackState mPlaybackState = PlaybackState::Stopped;
    float mVolume = 1.0f;
    float mPitch = 1.0f;
    float mSpatialBlend = 0.0f;
    float mMinDistance = 1.0f;
    float mMaxDistance = 15.0f;
    unsigned long long mPlaybackRevision = 0;
    bool mLoop = false;
    bool mPlayOnAwake = false;
};

}
