#include "AnimatorTests.h"

#include "FbxAnimationFixture.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Animation/AnimationClip.h"
#include "Animation/PoseSampler.h"
#include "Animation/Skeleton.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetReference.h"
#include "Assets/SkinnedMeshData.h"
#include "Math/Matrix.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Animator.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"
#include "SceneRendering/SceneRenderPass.h"
#include "TestSupport.h"

using namespace GameEngine;
using TestSupport::Expect;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

/// <summary>
/// Animator가 포즈 샘플링과 스킨드 메시 렌더링을 잇는지 확인한다. Game::Update가 흘린
/// 재생 시각으로 SceneRenderPass가 뽑은 뼈 행렬은 RenderFrame의 SkinnedMeshDraw에 실려야 한다.
///
/// mesh·skeleton·clip의 AssetReference는 실제 데이터베이스를 통해 해석되어야 한다. 이 시험은
/// 자체 생성한 binary FBX fixture를 임시 프로젝트에 쓰고 스캔한다.
/// 같은 fixture의 가중치·골격·클립 파싱은 FbxSkeletalImportTests.cpp가 별도로 검증한다.
/// </summary>
namespace
{
    // SceneCullingTests.cpp와 같은 1x1 RGBA PNG다. 재질 텍스처가 진짜 이미지여야 draw가 실린다.
    constexpr unsigned char OnePixelPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0x0F, 0x04, 0x00, 0x09, 0xFB, 0x03, 0xFD, 0xFB, 0x5E, 0x6B, 0x2B, 0x00, 0x00, 0x00, 0x00,
        0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

    [[nodiscard]] bool IsNear(
        const Math::Vector3& value, const Math::Vector3& expected, const float epsilon = 1e-3f)
    {
        return std::abs(value.GetX() - expected.GetX()) < epsilon &&
            std::abs(value.GetY() - expected.GetY()) < epsilon &&
            std::abs(value.GetZ() - expected.GetZ()) < epsilon;
    }

    /// <summary>
    /// 이 데이터베이스에서 그 종류의 첫 서브에셋을 가리키는 참조다. 목록이 비어 있으면 무효한
    /// 참조를 돌려주고, 그것을 부른 자리가 Expect로 잡는다.
    /// </summary>
    [[nodiscard]] Assets::AssetReference FirstChoiceOf(
        const Assets::AssetDatabase& database, const Assets::AssetType type)
    {
        const std::vector<Assets::AssetChoice> choices = Assets::CollectAssetChoices(database, type);
        return choices.empty() ? Assets::AssetReference{} : choices.front().reference;
    }

    /// <summary>여러 뼈 행렬을 하나의 실수로 접는다. 시각마다 자세가 같은지 다른지만 재는 자리라,
    /// 어느 뼈가 움직였는지는 몰라도 된다 — 복잡한 리그에서는 특정 뼈 하나가 매 클립마다 움직인다는
    /// 보장이 없기 때문이다.</summary>
    [[nodiscard]] float SumBoneTransforms(const std::vector<Math::Matrix4x4>& matrices)
    {
        float sum = 0.0f;
        for (const Math::Matrix4x4& matrix : matrices)
        {
            const Math::Vector3 point = matrix.TransformPoint({ 1.0f, 0.0f, 0.0f });
            sum += point.GetX() + point.GetY() + point.GetZ();
        }
        return sum;
    }
}

bool RunAnimatorTests()
{
    const std::vector<std::byte> fbx = TestSupport::FbxFixture::MakeSkinnedFixture();
    const std::string_view fbxBytes(reinterpret_cast<const char*>(fbx.data()), fbx.size());

    TemporaryDirectory projectDirectory("animator");
    const std::filesystem::path root = projectDirectory.GetPath();
    const bool wrote =
        WriteFile(root / "AnimatorTest.gameproject", "{}") &&
        WriteFile(root / "Sprites" / "tile.png",
            std::string_view(reinterpret_cast<const char*>(OnePixelPng), sizeof(OnePixelPng))) &&
        WriteFile(root / "Sprites" / "tile.png.meta",
            R"({"format": "gameengine-meta/1", "pixelsPerUnit": 1.0})") &&
        WriteFile(root / "Materials" / "monster.material", R"({"texture": "Sprites/tile.png"})") &&
        WriteFile(root / "Models" / "monster.fbx", fbxBytes);
    if (!Expect(wrote, "the animator test project should be written"))
    {
        return false;
    }

    const Platform::DirectoryContentSource content(root);
    Runtime::Game game{ nullptr, nullptr };
    if (!Expect(game.Initialize(content, {}), "the animator test project should open"))
    {
        return false;
    }

    const Assets::AssetDatabase& assetDatabase = game.GetAssetDatabase();
    const Assets::AssetReference meshReference =
        FirstChoiceOf(assetDatabase, Assets::AssetType::SkinnedMesh);
    const Assets::AssetReference skeletonReference =
        FirstChoiceOf(assetDatabase, Assets::AssetType::Skeleton);
    const Assets::AssetReference clipReference =
        FirstChoiceOf(assetDatabase, Assets::AssetType::AnimationClip);
    if (!Expect(
            meshReference.IsValid() && skeletonReference.IsValid() && clipReference.IsValid(),
            "the test FBX should register a skinned mesh, a skeleton, and at least one clip"))
    {
        return false;
    }
    if (!Expect(
            Assets::CollectAssetChoices(assetDatabase, Assets::AssetType::Mesh).empty(),
            "a skinned FBX should not also register its geometry as a plain static Mesh -- a "
            "second, un-posable copy in the choice list would only confuse whoever picks one"))
    {
        return false;
    }

    const std::shared_ptr<const Animation::Skeleton> skeletonData =
        assetDatabase.LoadSkeleton(skeletonReference);
    const std::shared_ptr<const Animation::AnimationClip> clipData =
        assetDatabase.LoadAnimationClip(clipReference);
    if (!Expect(
            skeletonData && clipData, "the referenced skeleton and clip should load their payloads"))
    {
        return false;
    }
    const bool animatesDuringObservedInterval = clipData->duration > 0.9f &&
        std::ranges::any_of(clipData->tracks, [](const Animation::BoneTrack& track)
        {
            return track.rotationKeys.size() >= 2 &&
                track.rotationKeys.front().time <= 0.5f && track.rotationKeys.back().time >= 0.9f &&
                track.rotationKeys.front().value.AngleTo(track.rotationKeys.back().value) > 1.0f;
        });
    if (!Expect(animatesDuringObservedInterval,
            "the generated clip must contain changing rotation keys across the Animator sample times"))
    {
        return false;
    }

    auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "AnimatorScene");

    Runtime::GameObject* const animated = scene->CreateGameObject("Animated");
    animated->GetTransform().SetPosition({ 3.0f, 0.0f, 0.0f });
    Runtime::Animator* const animator = animated->AddComponent<Runtime::Animator>();
    animator->SetMesh(meshReference);
    animator->SetSkeleton(skeletonReference);
    animator->SetClip(clipReference);
    animator->SetMaterial(Assets::AssetReference::Parse("Materials/monster.material"));

    // ---- CollectAssetReferences: 넷 다 보고해야 한다 (material은 Renderer가, 나머지 셋은
    // Animator 자신이 보고한다) ----
    {
        std::vector<Assets::AssetReference> references;
        animator->CollectAssetReferences(references);
        const auto contains = [&references](const Assets::AssetReference& reference)
        {
            return std::ranges::find(references, reference) != references.end();
        };
        bool passed = Expect(references.size() == 4,
            "CollectAssetReferences should report exactly material, mesh, skeleton, and clip");
        passed &= Expect(contains(meshReference), "CollectAssetReferences should report the mesh");
        passed &= Expect(
            contains(skeletonReference), "CollectAssetReferences should report the skeleton");
        passed &= Expect(contains(clipReference), "CollectAssetReferences should report the clip");
        passed &= Expect(
            contains(Assets::AssetReference::Parse("Materials/monster.material")),
            "CollectAssetReferences should report the material");
        if (!passed)
        {
            return false;
        }
    }

    // ---- Clone: 복제된 GameObject 위의 Animator가 같은 네 참조를 그대로 쥔다. Component::Clone
    // 자신은 GameObject만 부를 수 있는 사슬이라, 공개된 GameObject::Clone을 통해 잰다 — 장면
    // 저장·로드도 같은 속성 왕복을 타므로 이것이 그 증거를 겸한다. ----
    {
        const std::unique_ptr<Runtime::GameObject> cloned = animated->Clone();
        const Runtime::Animator* const clonedAnimator =
            cloned ? cloned->GetComponent<Runtime::Animator>() : nullptr;
        bool passed = Expect(clonedAnimator != nullptr, "cloning an Animator should yield an Animator");
        if (clonedAnimator)
        {
            passed &= Expect(
                clonedAnimator->GetMesh() == animator->GetMesh() &&
                    clonedAnimator->GetSkeleton() == animator->GetSkeleton() &&
                    clonedAnimator->GetClip() == animator->GetClip() &&
                    clonedAnimator->GetMaterial() == animator->GetMaterial(),
                "a cloned Animator should keep the same mesh, skeleton, clip, and material "
                "references as the original -- the same round trip a scene save/reload uses");
        }
        if (!passed)
        {
            return false;
        }
    }

    // 뼈나 형상 중 하나가 빠진 Animator는 그리지 않아야 한다 — 부분적으로 채워진 컴포넌트가
    // 조용히 잘못된 draw를 싣는 대신, 아예 프레임에서 빠진다.
    Runtime::GameObject* const noSkeleton = scene->CreateGameObject("NoSkeleton");
    Runtime::Animator* const noSkeletonAnimator = noSkeleton->AddComponent<Runtime::Animator>();
    noSkeletonAnimator->SetMesh(meshReference);
    noSkeletonAnimator->SetMaterial(Assets::AssetReference::Parse("Materials/monster.material"));

    Runtime::GameObject* const noMesh = scene->CreateGameObject("NoMesh");
    Runtime::Animator* const noMeshAnimator = noMesh->AddComponent<Runtime::Animator>();
    noMeshAnimator->SetSkeleton(skeletonReference);
    noMeshAnimator->SetClip(clipReference);
    noMeshAnimator->SetMaterial(Assets::AssetReference::Parse("Materials/monster.material"));

    // mesh·skeleton·clip은 유효하지만 material이 텍스처를 가리키는 Animator다.
    // Material 도입 전 형식의 장면도 크래시 없이 열리며, 잘못된 종류의 참조는 그리지 않는다.
    Runtime::GameObject* const wrongMaterialType = scene->CreateGameObject("WrongMaterialType");
    Runtime::Animator* const wrongMaterialTypeAnimator =
        wrongMaterialType->AddComponent<Runtime::Animator>();
    wrongMaterialTypeAnimator->SetMesh(meshReference);
    wrongMaterialTypeAnimator->SetSkeleton(skeletonReference);
    wrongMaterialTypeAnimator->SetClip(clipReference);
    wrongMaterialTypeAnimator->SetMaterial(Assets::AssetReference::Parse("Sprites/tile.png"));

    static_cast<void>(game.GetSceneManager().AddScene(std::move(scene)));

    SceneRendering::SceneRenderPass frontend{ nullptr };
    const auto collect = [&game, &frontend]() -> Rendering::RenderFrame
    {
        Rendering::RenderFrameBuilder builder;
        frontend.Collect(game, builder);
        return std::move(builder).Build();
    };

    bool passed = true;

    // ---- ① 첫 업데이트: PoseSampler가 낸 행렬이 SkinnedMeshDraw에 그대로 실린다 ----
    game.Update(0.5f);
    {
        const Rendering::RenderFrame frame = collect();
        const std::vector<const Rendering::SkinnedMeshDraw*> draws =
            frame.GetDraws<Rendering::SkinnedMeshDraw>(Rendering::RenderPass::Opaque);
        passed &= Expect(
            draws.size() == 1,
            "only the fully-configured Animator should have produced a draw");
        if (draws.size() == 1)
        {
            const Rendering::SkinnedMeshDraw& draw = *draws.front();
            passed &= Expect(
                IsNear(draw.localToWorld.GetTranslation(), { 3.0f, 0.0f, 0.0f }),
                "the draw's world transform should follow the GameObject's Transform");
            passed &= Expect(
                draw.boneMatrices != nullptr && draw.boneMatrices->size() == skeletonData->bones.size(),
                "the draw should carry one bone matrix per bone in the resolved skeleton");

            const std::vector<Math::Matrix4x4> expected =
                Animation::SamplePose(*skeletonData, *clipData, 0.5f);
            passed &= Expect(
                draw.boneMatrices != nullptr && expected.size() == draw.boneMatrices->size() &&
                    std::abs(SumBoneTransforms(*draw.boneMatrices) - SumBoneTransforms(expected)) < 1e-3f,
                "the draw's bone matrices should be exactly what PoseSampler computes for the "
                "elapsed time Update advanced, resolved through the same skeleton and clip");
        }
    }

    // ---- ② 시간이 더 흐르면 자세도 따라 바뀐다 ----
    if (!passed) return false;
    const float firstPose = SumBoneTransforms(*collect()
        .GetDraws<Rendering::SkinnedMeshDraw>(Rendering::RenderPass::Opaque)
        .front()->boneMatrices);
    game.Update(0.4f);
    const float secondPose = SumBoneTransforms(*collect()
        .GetDraws<Rendering::SkinnedMeshDraw>(Rendering::RenderPass::Opaque)
        .front()->boneMatrices);
    passed &= Expect(
        std::abs(firstPose - secondPose) > 1e-4f,
        "advancing time should visibly move the animated pose");

    // ---- ③ 멈추면 그 자리에 머문다 ----
    animator->SetPlaying(false);
    const float beforePause = secondPose;
    game.Update(0.3f);
    const float afterPause = SumBoneTransforms(*collect()
        .GetDraws<Rendering::SkinnedMeshDraw>(Rendering::RenderPass::Opaque)
        .front()->boneMatrices);
    passed &= Expect(
        std::abs(beforePause - afterPause) < 1e-4f,
        "pausing should freeze the pose instead of continuing to advance");

    return passed;
}

static const TestSupport::Registration gAnimatorTests{
    "Animator", "animator tests should pass", RunAnimatorTests };
