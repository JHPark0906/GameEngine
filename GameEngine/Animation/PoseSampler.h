#pragma once

#include <vector>

#include "../Math/Matrix.h"

namespace GameEngine::Animation
{

struct Skeleton;
struct AnimationClip;

/// <summary>
/// 골격을 애니메이션 클립으로 시각 <paramref name="time"/>(초)에 채점해, 뼈마다 하나씩
/// 오브젝트 공간 스키닝 행렬을 낸다 — SkinnedMeshDraw::boneMatrices(RenderFrame.h)가 그대로
/// 받는 순서와 공간이다: 각 행렬은 바인드 포즈의 오브젝트 공간 정점을, 그 뼈가 지금 자세에서
/// 있어야 할 자리로 옮긴다(바인드 포즈의 오브젝트 공간 역행렬 곱하기 지금 자세의 오브젝트 공간
/// 변환).
///
/// time은 clip.duration으로 감긴다(looping) — 음수도 안전하게 접힌다. 골격이나 클립이
/// 잘못됐으면(Skeleton::IsValid나 AnimationClip::IsValid가 거짓) 빈 벡터를 돌려준다. 트랙이
/// 없는 뼈, 또는 트랙은 있어도 어느 채널에 키가 없는 뼈는 그 채널만 바인드 포즈 그대로
/// 머문다.
/// </summary>
[[nodiscard]] std::vector<Math::Matrix4x4> SamplePose(
    const Skeleton& skeleton, const AnimationClip& clip, float time);

}
