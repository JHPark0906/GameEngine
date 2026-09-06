#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "../Math/Quaternion.h"
#include "../Math/Vector.h"

namespace GameEngine::Animation
{

/// <summary>한 채널의 값 하나가 어느 시각(초)에 놓이는지다.</summary>
template <typename T>
struct Key
{
    float time = 0.0f;
    T value{};
};

using PositionKey = Key<Math::Vector3>;
using RotationKey = Key<Math::Quaternion>;
using ScaleKey = Key<Math::Vector3>;

/// <summary>
/// 뼈 하나의 애니메이션 트랙이다. 세 채널은 독립적으로 키를 가질 수 있다 — 배율을 전혀 쓰지
/// 않는 애니메이션은 scaleKeys가 비어 있는 채로 자연스럽다. 비어 있는 채널은 골격이 가진 그
/// 뼈의 바인드 포즈 값을 그대로 쓴다(PoseSampler.h).
///
/// 각 키 배열은 시간 오름차순이어야 한다 — SamplePose가 앞에서부터 훑으며 보간할 구간을
/// 찾는다.
/// </summary>
struct BoneTrack
{
    /// <summary>이 트랙이 움직이는 뼈의, 골격 배열 안 인덱스다.</summary>
    std::uint32_t boneIndex = 0;
    std::vector<PositionKey> positionKeys;
    std::vector<RotationKey> rotationKeys;
    std::vector<ScaleKey> scaleKeys;
};

/// <summary>
/// 클립 하나다. 뼈마다 최대 하나의 트랙을 가지며, 트랙이 없는 뼈는 애니메이션 내내 바인드
/// 포즈에 머문다.
///
/// duration은 이 클립이 감기는(loop) 주기다(초). SamplePose는 재생 시각을 [0, duration)
/// 안으로 접어 넣은 뒤 채점한다 — 마지막 프레임에서 첫 프레임으로 매끄럽게 되돌아가는지는 그
/// 안의 키가 결정할 저작의 몫이다.
/// </summary>
struct AnimationClip
{
    std::string name;
    float duration = 0.0f;
    std::vector<BoneTrack> tracks;

    /// <summary>
    /// duration이 유한하고 음이 아니며, 모든 트랙의 모든 키 배열이 시간 오름차순(같은 시각
    /// 반복 허용)이고 시각이 유한하고 음이 아닌지다. 이것이 깨지면 SamplePose가 보간할 구간을
    /// 잘못 찾을 수 있다 — 뒤집히지는 않지만 저작자가 의도한 자세가 아니다.
    /// </summary>
    [[nodiscard]] bool IsValid() const
    {
        if (!std::isfinite(duration) || duration < 0.0f)
        {
            return false;
        }
        const auto keysAreOrdered = [](const auto& keys)
        {
            float previousTime = -1.0f;
            for (const auto& key : keys)
            {
                if (!std::isfinite(key.time) || key.time < 0.0f || key.time < previousTime)
                {
                    return false;
                }
                previousTime = key.time;
            }
            return true;
        };
        for (const BoneTrack& track : tracks)
        {
            if (!keysAreOrdered(track.positionKeys) || !keysAreOrdered(track.rotationKeys) ||
                !keysAreOrdered(track.scaleKeys))
            {
                return false;
            }
        }
        return true;
    }
};

}
