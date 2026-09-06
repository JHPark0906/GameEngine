#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "../Math/Quaternion.h"
#include "../Math/Vector.h"

namespace GameEngine::Animation
{

/// <summary>
/// 골격의 뼈 하나다. 부모 인덱스로 계층을 나타내고, 바인드 포즈에서의 부모 상대 SRT를 가진다.
/// </summary>
struct Bone
{
    /// <summary>이 뼈에게 부모가 없다는 뜻이다 — 뿌리 뼈다.</summary>
    static constexpr std::uint32_t NoParent = (std::numeric_limits<std::uint32_t>::max)();

    std::string name;
    /// <summary>부모 뼈의, 골격 배열 안 인덱스다. 뿌리면 NoParent다.</summary>
    std::uint32_t parentIndex = NoParent;

    /// <summary>바인드 포즈에서, 이 뼈의 부모 공간 기준 위치·회전·배율이다.</summary>
    Math::Vector3 bindPosePosition;
    Math::Quaternion bindPoseRotation = Math::Quaternion::Identity();
    Math::Vector3 bindPoseScale{ 1.0f, 1.0f, 1.0f };
};

/// <summary>
/// 한 메시가 매이는 뼈들의 계층이다. 배열 순서 자체가 계층의 증거다: 어느 뼈든 자기 부모보다
/// 뒤에 온다 — 그래서 앞에서부터 한 번 훑으면 부모의 포즈가 자식보다 먼저 갖춰진다.
///
/// 여기 담기는 것은 바인드 포즈뿐이다: 어떤 자세로도 이 골격을 스킨할 준비가 된 상태다. 실제로
/// 움직이는 자세는 <c>AnimationClip</c>과 <c>SamplePose</c>가 만든다(PoseSampler.h) — 이
/// 타입은 그 둘을 모른다.
/// </summary>
struct Skeleton
{
    std::vector<Bone> bones;

    /// <summary>
    /// 뼈가 있고, 모든 부모 인덱스가 자기보다 앞선 자리를 가리키거나 뿌리(NoParent)인지다.
    /// 이것이 성립해야 앞에서부터 한 번 훑는 포즈 계산이 옳다.
    /// </summary>
    [[nodiscard]] bool IsValid() const
    {
        if (bones.empty())
        {
            return false;
        }
        for (std::size_t index = 0; index < bones.size(); ++index)
        {
            const std::uint32_t parent = bones[index].parentIndex;
            if (parent == Bone::NoParent)
            {
                continue;
            }
            if (parent >= index)
            {
                return false;
            }
        }
        return true;
    }
};

}
