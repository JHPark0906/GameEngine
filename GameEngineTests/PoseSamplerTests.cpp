#include "PoseSamplerTests.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "Animation/AnimationClip.h"
#include "Animation/PoseSampler.h"
#include "Animation/Skeleton.h"
#include "Math/Matrix.h"
#include "Math/Quaternion.h"
#include "Math/Vector.h"
#include "TestSupport.h"

using TestSupport::Expect;

/// <summary>
/// 포즈 샘플러의 계약: 골격과 클립만으로, 렌더링도 창도 없이, 뼈마다 옳은 스키닝 행렬을
/// 낸다는 것이다. RuntimeObjectTests.cpp가 Transform 계층을 재는 것과 같은 방식으로 잰다 —
/// 행렬 자체가 아니라, 알려진 점을 그 행렬로 옮긴 결과를 비교한다.
/// </summary>
namespace
{
    [[nodiscard]] bool IsNear(
        const GameEngine::Math::Vector3& value, const GameEngine::Math::Vector3& expected,
        const float epsilon = 1e-3f)
    {
        return std::abs(value.GetX() - expected.GetX()) < epsilon &&
            std::abs(value.GetY() - expected.GetY()) < epsilon &&
            std::abs(value.GetZ() - expected.GetZ()) < epsilon;
    }

    /// <summary>뿌리 뼈 하나뿐인 골격이다. 바인드 포즈는 지정한 그대로다.</summary>
    [[nodiscard]] GameEngine::Animation::Skeleton MakeSingleBoneSkeleton(
        const GameEngine::Math::Vector3& bindPosition = {})
    {
        using namespace GameEngine::Animation;
        Skeleton skeleton;
        Bone root;
        root.name = "Root";
        root.bindPosePosition = bindPosition;
        skeleton.bones.push_back(root);
        return skeleton;
    }
}

bool RunPoseSamplerTests()
{
    using namespace GameEngine;
    using namespace GameEngine::Animation;

    bool passed = true;

    // 트랙이 없는 클립: 지금 자세가 곧 바인드 포즈이므로, 스킨 행렬은 항등이어야 한다 — 어느
    // 점을 옮겨도 그 자리 그대로다.
    {
        const Skeleton skeleton = MakeSingleBoneSkeleton({ 1.0f, 2.0f, 3.0f });
        const AnimationClip clip;
        const std::vector<Math::Matrix4x4> pose = SamplePose(skeleton, clip, 0.0f);
        passed &= Expect(pose.size() == 1, "an untracked pose should have one matrix per bone");
        passed &= Expect(
            !pose.empty() && IsNear(pose[0].TransformPoint({ 5.0f, 6.0f, 7.0f }), { 5.0f, 6.0f, 7.0f }),
            "a bone with no track should skin to the identity");
    }

    // 회전 트랙 하나: t=0.5에서 0도와 90도 사이를 구면 보간하면 45도가 나와야 한다. 위치·배율
    // 채널에는 키가 없으니 그 둘은 바인드 포즈(항등) 그대로다.
    {
        const Skeleton skeleton = MakeSingleBoneSkeleton();
        AnimationClip clip;
        clip.duration = 1.0f;
        BoneTrack track;
        track.boneIndex = 0;
        track.rotationKeys = {
            { 0.0f, Math::Quaternion::Identity() },
            { 1.0f, Math::Quaternion::FromAxisAngleDegrees({ 0.0f, 0.0f, 1.0f }, 90.0f) }
        };
        clip.tracks.push_back(track);

        const std::vector<Math::Matrix4x4> pose = SamplePose(skeleton, clip, 0.5f);
        const Math::Vector3 expected =
            Math::Quaternion::FromAxisAngleDegrees({ 0.0f, 0.0f, 1.0f }, 45.0f).Rotate({ 1.0f, 0.0f, 0.0f });
        passed &= Expect(
            !pose.empty() && IsNear(pose[0].TransformPoint({ 1.0f, 0.0f, 0.0f }), expected),
            "halfway between two rotation keys should be their slerp midpoint");
    }

    // 계층: 뿌리를 90도 돌리면, 뿌리에 매인 자식은 트랙이 없어도 그 회전에 실려 옮겨져야 한다.
    // 자식의 로컬 오프셋(2,0,0)은 바뀌지 않되, 어디를 가리키는지는 뿌리의 자세를 따른다.
    {
        Skeleton skeleton;
        Bone root;
        root.name = "Root";
        skeleton.bones.push_back(root);
        Bone child;
        child.name = "Child";
        child.parentIndex = 0;
        child.bindPosePosition = { 2.0f, 0.0f, 0.0f };
        skeleton.bones.push_back(child);

        AnimationClip clip;
        // duration은 마지막 키보다 길게 둔다: 재생 시각이 duration과 같으면 [0, duration)
        // 감기 규칙에 따라 0으로 접혀, t=1의 90도가 아니라 t=0의 항등이 나와 버린다 — 정확히
        // 마지막 키에서 표본을 뜨려면 duration이 그 키보다 늦어야 한다.
        clip.duration = 2.0f;
        BoneTrack rootTrack;
        rootTrack.boneIndex = 0;
        rootTrack.rotationKeys = {
            { 0.0f, Math::Quaternion::Identity() },
            { 1.0f, Math::Quaternion::FromAxisAngleDegrees({ 0.0f, 0.0f, 1.0f }, 90.0f) }
        };
        clip.tracks.push_back(rootTrack);

        const std::vector<Math::Matrix4x4> pose = SamplePose(skeleton, clip, 1.0f);
        const Math::Vector3 expected = Math::Matrix4x4::CreateRotationZDegrees(90.0f)
            .TransformPoint({ 2.0f, 0.0f, 0.0f });
        passed &= Expect(pose.size() == 2, "a two-bone skeleton should have two skin matrices");
        passed &= Expect(
            pose.size() == 2 && IsNear(pose[1].TransformPoint({ 2.0f, 0.0f, 0.0f }), expected),
            "an untracked child should be carried by its animated parent's rotation");
        passed &= Expect(
            pose.size() == 2 && IsNear(pose[0].TransformPoint({}), {}),
            "the rotating root's own origin should stay put under its own skin matrix");
    }

    // 감기(loop): 주기를 몇 바퀴 더하거나 음수로 줘도 같은 자세가 나와야 한다.
    {
        const Skeleton skeleton = MakeSingleBoneSkeleton();
        AnimationClip clip;
        clip.duration = 1.0f;
        BoneTrack track;
        track.boneIndex = 0;
        track.rotationKeys = {
            { 0.0f, Math::Quaternion::Identity() },
            { 1.0f, Math::Quaternion::FromAxisAngleDegrees({ 0.0f, 0.0f, 1.0f }, 90.0f) }
        };
        clip.tracks.push_back(track);

        const std::vector<Math::Matrix4x4> base = SamplePose(skeleton, clip, 0.5f);
        const std::vector<Math::Matrix4x4> wrappedForward = SamplePose(skeleton, clip, 0.5f + 3.0f);
        const std::vector<Math::Matrix4x4> wrappedNegative = SamplePose(skeleton, clip, 0.5f - 1.0f);
        const Math::Vector3 testPoint{ 1.0f, 0.0f, 0.0f };
        passed &= Expect(
            base.size() == 1 && wrappedForward.size() == 1 && wrappedNegative.size() == 1 &&
                IsNear(base[0].TransformPoint(testPoint), wrappedForward[0].TransformPoint(testPoint)) &&
                IsNear(base[0].TransformPoint(testPoint), wrappedNegative[0].TransformPoint(testPoint)),
            "sampling should wrap the same pose forward whole periods and back across zero");
    }

    // 채널별 빠짐: 회전만 애니메이션되는 트랙에서도 위치는 바인드 포즈(원점이 아닌 값)에
    // 머물러야 한다. 기대값은 SamplePose를 다시 부르지 않고, 같은 Matrix4x4 연산으로 이
    // 경우(단일 뼈, 계층 없음)를 직접 조립해 구한다 — SamplePose의 내부 반복이 옳다는 것을
    // 그 반복 자체로 증명하지 않기 위해서다.
    {
        Skeleton skeleton;
        Bone root;
        root.name = "Root";
        root.bindPosePosition = { 5.0f, 0.0f, 0.0f };
        skeleton.bones.push_back(root);

        AnimationClip clip;
        // 마지막 키(t=1)를 정확히 표본으로 뜨려면 duration이 그보다 길어야 한다 — 위 계층
        // 시험과 같은 이유다.
        clip.duration = 2.0f;
        const Math::Quaternion targetRotation =
            Math::Quaternion::FromAxisAngleDegrees({ 0.0f, 0.0f, 1.0f }, 90.0f);
        BoneTrack track;
        track.boneIndex = 0;
        track.rotationKeys = { { 0.0f, Math::Quaternion::Identity() }, { 1.0f, targetRotation } };
        clip.tracks.push_back(track);

        const std::vector<Math::Matrix4x4> pose = SamplePose(skeleton, clip, 1.0f);

        const Math::Matrix4x4 bindLocal = Math::Matrix4x4::CreateTranslation(root.bindPosePosition);
        const Math::Matrix4x4 currentLocal =
            Math::Matrix4x4::CreateRotation(targetRotation) *
            Math::Matrix4x4::CreateTranslation(root.bindPosePosition);
        Math::Matrix4x4 inverseBind = Math::Matrix4x4::Identity();
        passed &= Expect(bindLocal.TryInvert(inverseBind), "the bind matrix in this case should invert");
        const Math::Vector3 expected = (inverseBind * currentLocal).TransformPoint({});

        passed &= Expect(
            !pose.empty() && IsNear(pose[0].TransformPoint({}), expected),
            "an animated rotation channel should not disturb an unanimated position channel");
    }

    // 트랙 없는 형제: 뼈 하나가 도는 동안 트랙이 없는 다른 뼈는 그대로 있어야 한다.
    {
        Skeleton skeleton;
        Bone animated;
        animated.name = "Animated";
        Bone still;
        still.name = "Still";
        skeleton.bones.push_back(animated);
        skeleton.bones.push_back(still);

        AnimationClip clip;
        clip.duration = 1.0f;
        BoneTrack track;
        track.boneIndex = 0;
        track.rotationKeys = {
            { 0.0f, Math::Quaternion::Identity() },
            { 1.0f, Math::Quaternion::FromAxisAngleDegrees({ 0.0f, 0.0f, 1.0f }, 90.0f) }
        };
        clip.tracks.push_back(track);

        const std::vector<Math::Matrix4x4> pose = SamplePose(skeleton, clip, 1.0f);
        passed &= Expect(
            pose.size() == 2 && IsNear(pose[1].TransformPoint({ 3.0f, 4.0f, 5.0f }), { 3.0f, 4.0f, 5.0f }),
            "an untracked bone should stay at bind pose while a sibling animates");
    }

    // 잘못된 입력: 골격이 비었거나 부모 인덱스가 자기 자신 이후를 가리키면, 또는 클립의 키가
    // 시간 순이 아니거나 duration이 음수면, 빈 벡터를 낸다.
    {
        const Skeleton emptySkeleton;
        passed &= Expect(
            SamplePose(emptySkeleton, AnimationClip{}, 0.0f).empty(),
            "an empty skeleton should sample to nothing");

        Skeleton cyclicSkeleton;
        Bone selfParented;
        selfParented.parentIndex = 0;
        cyclicSkeleton.bones.push_back(selfParented);
        passed &= Expect(
            !cyclicSkeleton.IsValid() && SamplePose(cyclicSkeleton, AnimationClip{}, 0.0f).empty(),
            "a bone that is its own parent should be rejected, not composed forever");

        const Skeleton validSkeleton = MakeSingleBoneSkeleton();
        AnimationClip unsortedClip;
        BoneTrack unsortedTrack;
        unsortedTrack.boneIndex = 0;
        unsortedTrack.positionKeys = { { 1.0f, {} }, { 0.0f, {} } };
        unsortedClip.tracks.push_back(unsortedTrack);
        passed &= Expect(
            !unsortedClip.IsValid() && SamplePose(validSkeleton, unsortedClip, 0.0f).empty(),
            "a clip whose keys are not time-ordered should be rejected");

        AnimationClip negativeDurationClip;
        negativeDurationClip.duration = -1.0f;
        passed &= Expect(
            !negativeDurationClip.IsValid() &&
                SamplePose(validSkeleton, negativeDurationClip, 0.0f).empty(),
            "a clip with negative duration should be rejected");
    }

    return passed;
}

static const TestSupport::Registration gPoseSamplerTests{
    "PoseSampler", "pose sampler tests should pass", RunPoseSamplerTests };
