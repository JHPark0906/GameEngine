#include "AssetSubAssetTypeTests.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include "Assets/AssetDatabase.h"
#include "Assets/AssetImporter.h"
#include "Assets/AssetImporterRegistry.h"
#include "Assets/AssetReference.h"
#include "Platform/DirectoryContentSource.h"
#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::RegistryScope;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{
    using GameEngine::Assets::AssetType;

    /// <summary>
    /// 서로 다른 종류의 서브에셋을 내는 임포터를 흉내 낸다: 파일 하나가 메시·스켈레톤·애니메이션 클립을 함께
    /// 낸다. 파일의 대표 타입은(<see cref="GetAssetType"/>) 언제나 <c>Mesh</c>다 — 확장자
    /// 하나에는 임포터 하나뿐이고, 그 임포터가 답할 수 있는 타입도 하나뿐이기 때문이다. 실제로
    /// 몇 종류가 들었는지는 서브에셋 목록만 안다.
    /// </summary>
    class FakeSkeletalModelImporter final : public GameEngine::Assets::IAssetImporter
    {
    public:
        [[nodiscard]] AssetType GetAssetType() const override { return AssetType::Mesh; }

        [[nodiscard]] bool Import(
            const std::filesystem::path&,
            std::span<const std::byte>,
            const GameEngine::Assets::ImportIdentity&,
            const GameEngine::Assets::ImportMode,
            GameEngine::Assets::ImportedContents& contents,
            std::string&) const override
        {
            contents.subAssets.push_back({ AssetType::Mesh, "Body" });
            contents.subAssets.push_back({ AssetType::Skeleton, "Rig" });
            contents.subAssets.push_back({ AssetType::AnimationClip, "Walk" });
            return true;
        }
    };

    /// <summary>
    /// 확장자 하나가 정확히 하나의 형식을 내는 보통의 경우다. <c>Skeleton</c>·
    /// <c>AnimationClip</c>도 다른 형식들과 똑같이 파일의 대표 타입으로 설 수 있는지가
    /// 여기서 궁금한 것이다.
    /// </summary>
    class FakeSingleTypeImporter final : public GameEngine::Assets::IAssetImporter
    {
    public:
        explicit FakeSingleTypeImporter(const AssetType type) : mType(type) {}

        [[nodiscard]] AssetType GetAssetType() const override { return mType; }

        [[nodiscard]] bool Import(
            const std::filesystem::path& relativePath,
            std::span<const std::byte>,
            const GameEngine::Assets::ImportIdentity&,
            const GameEngine::Assets::ImportMode,
            GameEngine::Assets::ImportedContents& contents,
            std::string&) const override
        {
            contents.subAssets.push_back({ mType, relativePath.stem().string() });
            return true;
        }

    private:
        AssetType mType;
    };
}

bool RunAssetSubAssetTypeTests()
{
    namespace Assets = GameEngine::Assets;

    bool passed = true;

    // 🔴 이름 왕복. 새 타입도 다른 타입과 같은 표를 지나가는지부터 본다.
    passed &= Expect(
        Assets::AssetDatabase::GetAssetTypeName(AssetType::Skeleton) == "Skeleton" &&
            Assets::AssetDatabase::ParseAssetTypeName("Skeleton") == AssetType::Skeleton,
        "Skeleton should round-trip through its type name");
    passed &= Expect(
        Assets::AssetDatabase::GetAssetTypeName(AssetType::AnimationClip) == "AnimationClip" &&
            Assets::AssetDatabase::ParseAssetTypeName("AnimationClip") ==
                AssetType::AnimationClip,
        "AnimationClip should round-trip through its type name");

    // 새 타입이 파일 자신의 대표 타입으로도 설 수 있는지 — 서브에셋으로만 사는 것이 아니라는
    // 확인이다. 지금 임포터 중에 이렇게 선언하는 것은 없지만, 형식 하나가 만들어졌다면 이 자리도
    // 다른 형식과 같은 방식으로 동작해야 한다.
    {
        const RegistryScope registries;
        static const FakeSingleTypeImporter skeletonFileImporter(AssetType::Skeleton);
        static const FakeSingleTypeImporter animationFileImporter(AssetType::AnimationClip);
        const bool registered =
            Assets::AssetImporterRegistry::Register(".testskeleton", skeletonFileImporter) &&
            Assets::AssetImporterRegistry::Register(".testanim", animationFileImporter);

        TemporaryDirectory temporaryDirectory("standalone-skeletal-types");
        const std::filesystem::path root = temporaryDirectory.GetPath();
        const bool wrote =
            WriteFile(root / "StandaloneTest.gameproject", "{}") &&
            WriteFile(root / "Rig.testskeleton", "rig-bytes") &&
            WriteFile(root / "Walk.testanim", "clip-bytes");

        const GameEngine::Platform::DirectoryContentSource content(root);
        Assets::AssetDatabase database;
        const bool refreshed = registered && wrote && database.Refresh(content);

        const Assets::Asset* const rig = database.FindAsset("Rig.testskeleton");
        const Assets::Asset* const walk = database.FindAsset("Walk.testanim");
        passed &= Expect(
            refreshed && rig && rig->GetType() == AssetType::Skeleton,
            "a file whose importer declares Skeleton should register as a Skeleton asset");
        passed &= Expect(
            walk && walk->GetType() == AssetType::AnimationClip,
            "a file whose importer declares AnimationClip should register as an AnimationClip"
            " asset");
    }

    // 한 파일이 서로 다른 타입의 서브에셋을 함께 내는 경우다.
    {
        const RegistryScope registries;
        static const FakeSkeletalModelImporter modelImporter;
        const bool registered =
            Assets::AssetImporterRegistry::Register(".testskeletalmodel", modelImporter);

        TemporaryDirectory temporaryDirectory("mixed-subasset-types");
        const std::filesystem::path root = temporaryDirectory.GetPath();
        const std::filesystem::path modelPath = "Creature.testskeletalmodel";
        const bool wrote =
            WriteFile(root / "MixedTest.gameproject", "{}") &&
            WriteFile(root / modelPath, "model-bytes");

        const GameEngine::Platform::DirectoryContentSource content(root);
        Assets::AssetDatabase database;
        const bool refreshed = registered && wrote && database.Refresh(content);
        if (!Expect(refreshed, "the mixed-type model should scan"))
        {
            return false;
        }

        const Assets::Asset* const model = database.FindAsset(modelPath);
        passed &= Expect(
            model && model->GetType() == AssetType::Mesh,
            "the file's own type is still whatever its importer declares, Mesh here");

        // 파일이 낸 순서 그대로: 0 메시, 1 스켈레톤, 2 애니메이션 클립.
        const Assets::AssetReference meshRef(modelPath, 0);
        const Assets::AssetReference skeletonRef(modelPath, 1);
        const Assets::AssetReference animationRef(modelPath, 2);

        // 선택지 목록도 파일의 대표 타입이 아니라 각 서브에셋 타입으로 걸러야 한다.
        passed &= Expect(
            Assets::CollectAssetChoices(database, AssetType::Mesh).size() == 1 &&
                Assets::CollectAssetChoices(database, AssetType::Skeleton).size() == 1 &&
                Assets::CollectAssetChoices(database, AssetType::AnimationClip).size() == 1,
            "each sub-asset type should offer exactly the one choice it has");

        // 참조가 가리키는 서브에셋 자신의 타입으로 걸러야 한다.
        // 파일 대표 타입만 사용하면 스켈레톤과 애니메이션 클립의 참조가 자기 타입 슬롯에서도 거절된다.
        passed &= Expect(
            Assets::AssetReferenceMatchesType(database, meshRef, AssetType::Mesh) &&
                !Assets::AssetReferenceMatchesType(database, meshRef, AssetType::Skeleton) &&
                !Assets::AssetReferenceMatchesType(database, meshRef, AssetType::AnimationClip),
            "the mesh sub-asset should match a Mesh slot and no other kind");
        passed &= Expect(
            Assets::AssetReferenceMatchesType(database, skeletonRef, AssetType::Skeleton) &&
                !Assets::AssetReferenceMatchesType(database, skeletonRef, AssetType::Mesh),
            "the skeleton sub-asset should match a Skeleton slot, not the file's own Mesh type");
        passed &= Expect(
            Assets::AssetReferenceMatchesType(
                database, animationRef, AssetType::AnimationClip) &&
                !Assets::AssetReferenceMatchesType(database, animationRef, AssetType::Mesh) &&
                !Assets::AssetReferenceMatchesType(database, animationRef, AssetType::Skeleton),
            "the animation clip sub-asset should match an AnimationClip slot, not Mesh or"
            " Skeleton");

        // 종류를 묻지 않는 자리는 여전히 무엇이든 받는다.
        passed &= Expect(
            Assets::AssetReferenceMatchesType(database, skeletonRef, std::nullopt) &&
                Assets::AssetReferenceMatchesType(database, animationRef, std::nullopt),
            "a slot that names no kind should still take any resolvable sub-asset");
    }

    return passed;
}

static const TestSupport::Registration gAssetSubAssetTypeTests{
    "AssetDatabase", "asset sub-asset type tests should pass", RunAssetSubAssetTypeTests };
