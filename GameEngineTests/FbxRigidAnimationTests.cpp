#include "FbxRigidAnimationTests.h"

#include "FbxAnimationFixture.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

#include "Animation/PoseSampler.h"
#include "Assets/AssetImporterRegistry.h"
#include "Assets/FbxImporter.h"
#include "Math/Matrix.h"
#include "Math/Quaternion.h"
#include "TestSupport.h"

namespace
{
    using Bytes = std::vector<std::byte>;
    using namespace GameEngine;
    using TestSupport::Expect;

    using TestSupport::FbxFixture::FixtureOptions;
    using TestSupport::FbxFixture::MakeFixture;

    bool Near(const Math::Vector3& a, const Math::Vector3& b, const float epsilon = 0.005f)
    {
        return std::abs(a.GetX() - b.GetX()) < epsilon && std::abs(a.GetY() - b.GetY()) < epsilon &&
            std::abs(a.GetZ() - b.GetZ()) < epsilon;
    }

    bool CheckRigid(const bool animateParent)
    {
        FixtureOptions options;
        options.animateParent = animateParent;
        const Bytes bytes = MakeFixture(options);
        std::vector<Assets::ImportedMesh> staticMeshes;
        std::vector<Assets::SkinnedMeshData> meshes;
        std::vector<std::string> names;
        Animation::Skeleton skeleton;
        std::vector<Animation::AnimationClip> clips;
        std::string error;
        bool rigid = false;
        bool passed = Expect(Assets::FbxImporter::Load(bytes, staticMeshes, error), "synthetic static FBX should load");
        passed &= Expect(Assets::FbxImporter::LoadSkeleton(bytes, meshes, names, skeleton, clips, error, &rigid),
            ("rigid Model animation should import: " + error).c_str());
        if (!passed || meshes.size() != 1 || clips.size() != 1 || staticMeshes.size() != 1) return false;
        passed &= Expect(rigid && skeleton.IsValid() && skeleton.bones.size() == 2 && clips[0].IsValid(),
            "rigid meshes should include their non-joint Model ancestors in a valid skeleton");
        passed &= Expect(std::abs(clips[0].duration - 2.0f) < 1e-6f,
            "rigid clip should start at its first authored key and discard the two-second pre-roll");
        const auto& vertices = meshes[0].vertices;
        const std::uint32_t childBone = vertices.front().boneIndices[0];
        const auto bindPose = Animation::SamplePose(skeleton, Animation::AnimationClip{}, 0.0f);
        if (bindPose.size() != skeleton.bones.size()) return false;
        for (std::size_t index = 0; index < vertices.size(); ++index)
        {
            const auto& vertex = vertices[index];
            const Math::Vector3 position{ vertex.position.x, vertex.position.y, vertex.position.z };
            const auto& original = staticMeshes[0].vertices[index];
            passed &= Expect(vertex.boneWeights.x == 1.0f && vertex.boneWeights.y == 0.0f &&
                vertex.boneIndices[0] == childBone && Near(position, { original.position[0], original.position[1], original.position[2] }) &&
                Near(bindPose[childBone].TransformPoint(position), position),
                "rigid weight-one geometry and inverse bind pose should preserve static geometry exactly");
        }
        const auto rotationTrackIterator = std::ranges::find_if(clips[0].tracks, [animateParent, childBone](const auto& track)
        {
            return track.boneIndex == (animateParent ? 0 : childBone);
        });
        if (rotationTrackIterator == clips[0].tracks.end()) return false;
        const auto& rotationTrack = *rotationTrackIterator;
        passed &= Expect(rotationTrack.rotationKeys.front().time == 0.0f && rotationTrack.rotationKeys.size() >= 241,
            "rigid rotations should be normalized and densely baked");
        float totalAngle = 0.0f;
        for (std::size_t index = 1; index < rotationTrack.rotationKeys.size(); ++index)
            totalAngle += rotationTrack.rotationKeys[index - 1].value.AngleTo(rotationTrack.rotationKeys[index].value);
        passed &= Expect(std::abs(totalAngle - 1080.0f) < 0.5f, "baked quaternion keys should retain all three complete turns");
        for (const float time : { 0.37f, 0.5f, 1.25f, 1.75f })
        {
            const double u = time / 2.0;
            // Independent Hermite reference: p0=0, p1=1080, m0=900 deg/s, m1=180 deg/s.
            const float angle = static_cast<float>((u * u * u - 2 * u * u + u) * 1800 +
                (-2 * u * u * u + 3 * u * u) * 1080 + (u * u * u - u * u) * 360);
            const float pivotX = animateParent ? 10.0f : 13.0f;
            const auto expected = Math::Matrix4x4::CreateTranslation({ -pivotX, 0, 0 }) *
                Math::Matrix4x4::CreateRotationYDegrees(-angle) * Math::Matrix4x4::CreateTranslation({ pivotX, 0, 0 });
            const auto pose = Animation::SamplePose(skeleton, clips[0], time);
            passed &= Expect(pose.size() == skeleton.bones.size() &&
                Near(pose[childBone].TransformPoint({ 15, 0, 0 }), expected.TransformPoint({ 15, 0, 0 })),
                "sampled rigid pose should follow the authored cubic Euler rotation about the correct Model pivot");
        }
        return passed;
    }
}

bool RunFbxRigidAnimationTests()
{
    using namespace GameEngine;
    using TestSupport::Expect;
    bool passed = CheckRigid(true) & CheckRigid(false);
    const auto* importer = Assets::AssetImporterRegistry::Find("turns.fbx");
    if (!Expect(importer != nullptr, "FBX importer should be registered")) return false;
    for (const bool skin : { false, true })
    {
        FixtureOptions options;
        options.skin = skin;
        options.animateParent = false;
        for (const auto mode : { Assets::ImportMode::Structure, Assets::ImportMode::Full })
        {
            Assets::ImportedContents contents;
            std::string error;
            passed &= Expect(importer->Import("turns.fbx", MakeFixture(options), { 1, 2 }, mode, contents, error),
                "both structural and full FBX imports should succeed");
            const std::size_t firstSkinned = skin ? 0 : 1;
            passed &= Expect(contents.subAssets.size() == firstSkinned + 3 &&
                contents.subAssets[firstSkinned].type == Assets::AssetType::SkinnedMesh &&
                contents.subAssets[firstSkinned + 1].type == Assets::AssetType::Skeleton &&
                contents.subAssets[firstSkinned + 2].type == Assets::AssetType::AnimationClip &&
                (skin || contents.subAssets[0].type == Assets::AssetType::Mesh),
                "rigid imports must retain static localId zero while existing Skin files retain skinned localId zero");
            if (mode == Assets::ImportMode::Full && contents.subAssets.size() == firstSkinned + 3)
                passed &= Expect(contents.skinnedMeshes[firstSkinned] && contents.skeletons[firstSkinned + 1] &&
                    contents.animationClips[firstSkinned + 2] && (skin || contents.meshes[0]),
                    "full payload slots should match stable sub-asset indices");
        }
    }
    for (int failure = 0; failure < 5; ++failure)
    {
        FixtureOptions options;
        if (failure == 0) options.animated = false;
        if (failure == 1) options.angle = 1e12f;
        if (failure == 2) options.references = (std::numeric_limits<std::int32_t>::max)();
        if (failure == 3) options.flags |= 0x01000000;
        if (failure == 4) options.angle = (std::numeric_limits<float>::infinity)();
        std::vector<Assets::SkinnedMeshData> meshes(1);
        std::vector<std::string> names{ "old" };
        Animation::Skeleton skeleton;
        std::vector<Animation::AnimationClip> clips(1);
        std::string error;
        bool rigid = true;
        passed &= Expect(!Assets::FbxImporter::LoadSkeleton(MakeFixture(options), meshes, names, skeleton, clips, error, &rigid) &&
            !rigid && meshes.empty() && names.empty() && skeleton.bones.empty() && clips.empty() && !error.empty(),
            "static, over-budget, malformed or unsupported rigid clips should fail with cleared outputs and an error");
    }
    return passed;
}

static const TestSupport::Registration gFbxRigidAnimationTests{
    "FbxSkeletalImport", "rigid FBX animation tests should pass", RunFbxRigidAnimationTests };
