#include "FbxSkeletalImportTests.h"

#include "FbxAnimationFixture.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Animation/AnimationClip.h"
#include "Animation/Skeleton.h"
#include "Assets/FbxImporter.h"
#include "Assets/SkinnedMeshData.h"
#include "Math/Aabb3D.h"
#include "Math/Matrix.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    [[nodiscard]] std::vector<std::byte> ReadBinaryFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) return {};
        const std::streamsize size = stream.tellg();
        if (size <= 0) return {};
        stream.seekg(0, std::ios::beg);
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.read(reinterpret_cast<char*>(bytes.data()), size);
        return bytes;
    }

    /// <summary>
    /// 뼈마다 오브젝트 공간 위치다 — SamplePose(PoseSampler.h)와 같은 방식으로 부모 상대
    /// 바인드 포즈를 앞에서부터 이어 짠다. 임포터가 뼈와 정점을 서로 다른 공간에 놓는 결함은
    /// 위상이나 가중치 합만으로는 드러나지 않으므로, 이 시험은 그 결함이 낳을 결과 —
    /// 메시 밖으로 튀어나온 뼈 — 를 직접 잰다.
    /// </summary>
    [[nodiscard]] std::vector<GameEngine::Math::Vector3> ComputeBoneObjectSpacePositions(
        const GameEngine::Animation::Skeleton& skeleton)
    {
        using namespace GameEngine;
        std::vector<Math::Matrix4x4> globals(skeleton.bones.size());
        std::vector<Math::Vector3> positions(skeleton.bones.size());
        for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
        {
            const Animation::Bone& bone = skeleton.bones[index];
            const Math::Matrix4x4 local = Math::Matrix4x4::CreateScale(bone.bindPoseScale) *
                Math::Matrix4x4::CreateRotation(bone.bindPoseRotation) *
                Math::Matrix4x4::CreateTranslation(bone.bindPosePosition);
            globals[index] = bone.parentIndex == Animation::Bone::NoParent
                ? local
                : local * globals[bone.parentIndex];
            positions[index] = globals[index].GetTranslation();
        }
        return positions;
    }

    bool CheckSkeletalImport(const std::vector<std::byte>& fileBytes,
        const std::size_t expectedBones, const std::size_t expectedClips)
    {
        using namespace GameEngine;
        using namespace GameEngine::Assets;

        bool passed = Expect(!fileBytes.empty(), "the test FBX file should contain bytes");
        if (!passed) return false;

        std::vector<SkinnedMeshData> meshes;
        std::vector<std::string> meshNames;
        Animation::Skeleton skeleton;
        std::vector<Animation::AnimationClip> clips;
        std::string error;
        const bool loaded =
            FbxImporter::LoadSkeleton(fileBytes, meshes, meshNames, skeleton, clips, error);
        passed &= Expect(loaded, ("LoadSkeleton should succeed on the skeletal fixture: " + error).c_str());
        if (!loaded) return false;
        passed &= Expect(meshNames.size() == meshes.size(),
            "LoadSkeleton should name every skinned mesh it produces");

        // ⑴ 골격: 위상 순서(부모가 자식보다 앞)와 이름 있는 뼈들.
        passed &= Expect(skeleton.IsValid(), "the imported skeleton should be topologically valid");
        passed &= Expect(skeleton.bones.size() == expectedBones,
            "the fixture should retain its exact expected bone count");
        bool everyBoneNamed = true;
        for (const Animation::Bone& bone : skeleton.bones)
        {
            if (bone.name.empty()) everyBoneNamed = false;
        }
        passed &= Expect(everyBoneNamed, "every imported bone should carry the name FBX gave it");

        // ⑵ 스킨드 메시: 정점마다 뼈 가중치의 합이 1이어야 한다 — 스킨 셰이더가 이것을 가정한다.
        passed &= Expect(!meshes.empty(), "the file's skinned geometry should produce at least one mesh");
        bool allWeightsNormalized = true;
        bool everyBoneIndexInRange = true;
        for (const SkinnedMeshData& mesh : meshes)
        {
            for (const Assets::SkinnedMeshVertex& vertex : mesh.vertices)
            {
                const float total = vertex.boneWeights.x + vertex.boneWeights.y + vertex.boneWeights.z +
                    vertex.boneWeights.w;
                if (std::abs(total - 1.0f) > 1e-3f) allWeightsNormalized = false;
                for (const std::uint32_t boneIndex : vertex.boneIndices)
                {
                    if (boneIndex >= skeleton.bones.size()) everyBoneIndexInRange = false;
                }
            }
        }
        passed &= Expect(allWeightsNormalized, "every skinned vertex's bone weights should sum to 1");
        passed &= Expect(everyBoneIndexInRange, "every skinned vertex's bone indices should name a real bone");

        // ⑵-b 뼈와 정점이 같은 공간에 서는가. 이것이 어긋나면 위상도 가중치 합도 여전히 옳게
        // 보이지만 스킨은 메시를 엉뚱한 자리로 당긴다 — 그 결함이 남기는 눈에 보이는 흔적을 직접
        // 잰다: 바인드 포즈에서 뼈들은 자신이 움직이는 메시의 경계 안(약간의 여유를 두고) 있어야
        // 한다.
        {
            Math::Aabb3D meshBounds = Math::Aabb3D::Empty();
            for (const SkinnedMeshData& mesh : meshes) meshBounds = meshBounds.UnitedWith(mesh.bounds);
            const Math::Vector3 margin{
                meshBounds.GetSize().GetX() * 0.5f + 1.0f, meshBounds.GetSize().GetY() * 0.5f + 1.0f,
                meshBounds.GetSize().GetZ() * 0.5f + 1.0f };
            const Math::Aabb3D generousBounds = Math::Aabb3D::FromCenterSize(
                meshBounds.GetCenter(), meshBounds.GetSize() + margin + margin);
            bool everyBoneNearMesh = true;
            for (const Math::Vector3& position : ComputeBoneObjectSpacePositions(skeleton))
            {
                if (!generousBounds.Contains(position)) everyBoneNearMesh = false;
            }
            passed &= Expect(everyBoneNearMesh,
                "every bone's bind-pose position should land near the mesh it skins, not in an "
                "unrelated space");
        }

        // ⑶ 애니메이션 클립: AnimationStack마다 하나씩이되, 트랙 없는 스택은 걸러진다.
        passed &= Expect(clips.size() == expectedClips, "the fixture should retain its exact expected clip count");
        bool everyClipValid = true;
        bool everyClipNamedAndTracked = true;
        for (const Animation::AnimationClip& clip : clips)
        {
            if (!clip.IsValid()) everyClipValid = false;
            if (clip.name.empty() || clip.tracks.empty()) everyClipNamedAndTracked = false;
        }
        passed &= Expect(everyClipValid, "every imported clip should be internally valid (IsValid())");
        passed &= Expect(everyClipNamedAndTracked, "every imported clip should have a name and at least one track");

        // ⑷ 되돌림 시험: 존재하지 않는 파일 바이트는 실패해야 한다 — 성공 경로만 재는 시험은 실패도
        // 성공으로 읽는 임포터를 잡지 못한다.
        {
            const std::vector<std::byte> empty;
            std::vector<SkinnedMeshData> emptyMeshes;
            std::vector<std::string> emptyMeshNames;
            Animation::Skeleton emptySkeleton;
            std::vector<Animation::AnimationClip> emptyClips;
            std::string emptyError;
            const bool emptyLoaded = FbxImporter::LoadSkeleton(
                empty, emptyMeshes, emptyMeshNames, emptySkeleton, emptyClips, emptyError);
            passed &= Expect(!emptyLoaded && !emptyError.empty(),
                "LoadSkeleton should reject an empty file with a non-empty error message");
        }

        return passed;
    }

}

bool RunFbxSkeletalImportTests()
{
    return CheckSkeletalImport(TestSupport::FbxFixture::MakeSkinnedFixture(), 1, 1);
}

bool RunExternalFbxSkeletalImportTests(const std::filesystem::path& fixture)
{
    const std::vector<std::byte> bytes = ReadBinaryFile(fixture);
    if (!Expect(!bytes.empty(), "the explicitly configured external FBX must be readable"))
    {
        return false;
    }
    // This optional integration fixture has a fixed contract: 34 bones and five clips.
    return CheckSkeletalImport(bytes, 34, 5);
}

static const TestSupport::Registration gFbxSkeletalImportTests{
    "FbxSkeletalImport", "fbx skeletal import tests should pass", RunFbxSkeletalImportTests };
