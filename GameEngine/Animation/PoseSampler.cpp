#include "pch.h"
#include "PoseSampler.h"

#include <cmath>
#include <cstddef>

#include "AnimationClip.h"
#include "Skeleton.h"

namespace GameEngine::Animation
{

namespace
{
    /// <summary>
    /// 한 채널의 키들 사이에서 시각 time의 값을 낸다. 키가 없으면 바인드 포즈 값을, time이
    /// 첫 키보다 앞이거나 마지막 키보다 뒤면 그 끝의 값을 그대로 낸다 — 붙잡아 두는(hold) 것이
    /// 애니메이션의 표준 관례다. 두 키 사이면 interpolate로 보간한다.
    ///
    /// 세 채널(위치·회전·배율)이 찾고 붙잡고 보간하는 절차는 완전히 같고, 다른 것은 값의
    /// 타입과 보간 함수(Lerp 대 Slerp)뿐이라 여기 하나로 서 있다.
    /// </summary>
    template <typename T, typename InterpolateFn>
    [[nodiscard]] T SampleTrack(
        const std::vector<Key<T>>& keys, const float time, const T& bindPoseValue,
        InterpolateFn interpolate)
    {
        if (keys.empty())
        {
            return bindPoseValue;
        }
        if (time <= keys.front().time)
        {
            return keys.front().value;
        }
        if (time >= keys.back().time)
        {
            return keys.back().value;
        }
        for (std::size_t index = 0; index + 1 < keys.size(); ++index)
        {
            const Key<T>& a = keys[index];
            const Key<T>& b = keys[index + 1];
            if (time >= a.time && time <= b.time)
            {
                const float span = b.time - a.time;
                const float fraction = span > 0.0f ? (time - a.time) / span : 0.0f;
                return interpolate(a.value, b.value, fraction);
            }
        }
        // 키가 시간 오름차순이라는 계약(AnimationClip::IsValid)이 깨진 경우에만 여기 닿는다.
        return keys.back().value;
    }

}

std::vector<Math::Matrix4x4> SamplePose(
    const Skeleton& skeleton, const AnimationClip& clip, const float time)
{
    if (!skeleton.IsValid() || !clip.IsValid())
    {
        return {};
    }

    float wrappedTime = 0.0f;
    if (clip.duration > 0.0f)
    {
        wrappedTime = std::fmod(time, clip.duration);
        if (wrappedTime < 0.0f)
        {
            wrappedTime += clip.duration;
        }
    }

    const std::size_t boneCount = skeleton.bones.size();
    std::vector<Math::Matrix4x4> currentObjectSpace(boneCount);
    std::vector<Math::Matrix4x4> bindObjectSpace(boneCount);

    for (std::size_t index = 0; index < boneCount; ++index)
    {
        const Bone& bone = skeleton.bones[index];

        Math::Vector3 position = bone.bindPosePosition;
        Math::Quaternion rotation = bone.bindPoseRotation;
        Math::Vector3 scale = bone.bindPoseScale;

        const BoneTrack* track = nullptr;
        for (const BoneTrack& candidate : clip.tracks)
        {
            if (candidate.boneIndex == static_cast<std::uint32_t>(index))
            {
                track = &candidate;
                break;
            }
        }
        if (track)
        {
            position = SampleTrack(
                track->positionKeys, wrappedTime, bone.bindPosePosition, &Math::Vector3::Lerp);
            rotation = SampleTrack(
                track->rotationKeys, wrappedTime, bone.bindPoseRotation, &Math::Quaternion::Slerp);
            scale = SampleTrack(
                track->scaleKeys, wrappedTime, bone.bindPoseScale, &Math::Vector3::Lerp);
        }

        const Math::Matrix4x4 currentLocal =
            Math::Matrix4x4::CreateScale(scale) * Math::Matrix4x4::CreateRotation(rotation) *
            Math::Matrix4x4::CreateTranslation(position);
        const Math::Matrix4x4 bindLocal =
            Math::Matrix4x4::CreateScale(bone.bindPoseScale) *
            Math::Matrix4x4::CreateRotation(bone.bindPoseRotation) *
            Math::Matrix4x4::CreateTranslation(bone.bindPosePosition);

        if (bone.parentIndex == Bone::NoParent)
        {
            currentObjectSpace[index] = currentLocal;
            bindObjectSpace[index] = bindLocal;
        }
        else
        {
            // 행 벡터 규약: local을 먼저 적용하고 그 뒤 부모의 오브젝트 공간 변환을 적용한다 —
            // Runtime::Transform::GetLocalToWorldMatrix와 같은 산술이다.
            currentObjectSpace[index] = currentLocal * currentObjectSpace[bone.parentIndex];
            bindObjectSpace[index] = bindLocal * bindObjectSpace[bone.parentIndex];
        }
    }

    std::vector<Math::Matrix4x4> skinMatrices(boneCount);
    for (std::size_t index = 0; index < boneCount; ++index)
    {
        Math::Matrix4x4 inverseBind = Math::Matrix4x4::Identity();
        if (!bindObjectSpace[index].TryInvert(inverseBind))
        {
            // 바인드 포즈가 뒤집을 수 없을 만큼 퇴화했다(배율 0 등) — 그 뼈는 항등으로 남겨,
            // 적어도 나머지 뼈의 스키닝을 막지 않는다.
            skinMatrices[index] = Math::Matrix4x4::Identity();
            continue;
        }
        skinMatrices[index] = inverseBind * currentObjectSpace[index];
    }
    return skinMatrices;
}

}
