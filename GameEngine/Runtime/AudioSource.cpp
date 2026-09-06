#include "pch.h"
#include "AudioSource.h"
#include "PropertyDescriptor.h"
#include "../Diagnostics/Debug.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <utility>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// AudioSource가 선언하는 속성들이다. 이름은 장면 파일 형식이므로 기존 파일이 읽히는
    /// 그대로다. clip에 OmitWhenInvalid가 없는 것도 형식의 일부다: 현재 쓰기는 빈 클립도 항상
    /// 쓴다.
    /// </summary>
    std::span<const PropertyDescriptor> AudioSourceProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeAssetProperty<AudioSource>(
                "clip", "Clip", Assets::AssetType::AudioClip, &AudioSource::GetClip, &AudioSource::SetClip),
            MakeProperty<AudioSource>(
                "volume", "Volume", &AudioSource::GetVolume, &AudioSource::SetVolume),
            MakeProperty<AudioSource>(
                "pitch", "Pitch", &AudioSource::GetPitch, &AudioSource::SetPitch),
            MakeProperty<AudioSource>(
                "spatialBlend", "Spatial Blend",
                &AudioSource::GetSpatialBlend, &AudioSource::SetSpatialBlend),
            MakeProperty<AudioSource>(
                "minDistance", "Min Distance",
                &AudioSource::GetMinDistance, &AudioSource::SetMinDistance),
            MakeProperty<AudioSource>(
                "maxDistance", "Max Distance",
                &AudioSource::GetMaxDistance, &AudioSource::SetMaxDistance),
            MakeProperty<AudioSource>(
                "loop", "Loop", &AudioSource::IsLooping, &AudioSource::SetLooping),
            MakeProperty<AudioSource>(
                "playOnAwake", "Play On Awake",
                &AudioSource::GetPlayOnAwake, &AudioSource::SetPlayOnAwake),
        };
        return properties;
    }
}

const ComponentType& AudioSource::StaticType()
{
    static const ComponentType type{
        "AudioSource", &Behaviour::StaticType(), &AudioSourceProperties,
        &MakeComponentInstance<AudioSource> };
    return type;
}

void AudioSource::SetClip(Assets::AssetReference clip)
{
    if (mClip == clip)
    {
        return;
    }
    mClip = std::move(clip);
    mClipDurationSeconds = 0.0;
    // 재생 상태가 그대로여도 다른 클립의 보이스와 실패 기록은 다음 동기화에서 버려야 한다.
    ++mClipRevision;
}

void AudioSource::SetVolume(const float volume)
{
    mVolume = std::clamp(volume, 0.0f, 1.0f);
}

void AudioSource::SetPitch(const float pitch)
{
    mPitch = std::clamp(pitch, 0.01f, 4.0f);
}

void AudioSource::SetSpatialBlend(const float spatialBlend)
{
    mSpatialBlend = std::clamp(spatialBlend, 0.0f, 1.0f);
}

void AudioSource::SetMinDistance(const float minDistance)
{
    const float distance = std::isfinite(minDistance) ? (std::max)(minDistance, 0.0f) : 0.0f;
    mMinDistance = IsRestoringProperties() ? distance : (std::min)(distance, mMaxDistance);
}

void AudioSource::SetMaxDistance(const float maxDistance)
{
    const float distance = std::isfinite(maxDistance) ? (std::max)(maxDistance, 0.0f) : 0.0f;
    mMaxDistance = IsRestoringProperties() ? distance : (std::max)(distance, mMinDistance);
}

void AudioSource::OnPropertiesRestored() noexcept
{
    mMaxDistance = (std::max)(mMaxDistance, mMinDistance);
}

bool AudioSource::Play()
{
    if (!mClip.IsValid())
    {
        Diagnostics::Debug::LogError("AudioSource cannot play without an audio clip path.");
        return false;
    }

    mPlaybackState = PlaybackState::Playing;
    ++mPlaybackRevision;
    return true;
}

bool AudioSource::Pause()
{
    if (mPlaybackState != PlaybackState::Playing)
    {
        return false;
    }

    mPlaybackState = PlaybackState::Paused;
    ++mPlaybackRevision;
    return true;
}

void AudioSource::Stop()
{
    if (mPlaybackState != PlaybackState::Stopped)
    {
        mPlaybackState = PlaybackState::Stopped;
        ++mPlaybackRevision;
    }
}

void AudioSource::OnAttached()
{
    if (mPlayOnAwake && mClip.IsValid())
    {
        (void)Play();
    }
}

void AudioSource::OnRemoved()
{
    Stop();
}

}
