#include "AssetDatabaseTests.h"

#include "Assets/AssetMoveMatching.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Diagnostics/Debug.h"
#include "TestSupport.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImporter.h"
#include "Core/Guid.h"
#include "Core/ResourceId.h"
#include "Assets/AssetImporterRegistry.h"
#include "Assets/AssetReference.h"
#include "Assets/MeshData.h"
#include "Core/ResourceId.h"
#include "Platform/DirectoryContentSource.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/MeshRenderer.h"
#include "Runtime/ModelInstantiation.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/Transform.h"

using TestSupport::Expect;

using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{

    /// <summary>
    /// 여러 에셋을 담는 포맷이다. 그래서 진짜 모델 파일 없이 다중 에셋 경로를 시험할 수 있다.
    /// 프로젝트가 자기 포맷을 가져올 때 추가하는 것이기도 하다.
    /// </summary>
    class FakeModelImporter final : public GameEngine::Assets::IAssetImporter
    {
    public:
        [[nodiscard]] GameEngine::Assets::AssetType GetAssetType() const override
        {
            return GameEngine::Assets::AssetType::Mesh;
        }

        [[nodiscard]] bool Import(
            const std::filesystem::path& relativePath,
            std::span<const std::byte>,
            const GameEngine::Assets::ImportIdentity& identity,
            const GameEngine::Assets::ImportMode mode,
            GameEngine::Assets::ImportedContents& contents,
            std::string& error) const override
        {
            if (relativePath.stem() == "Broken")
            {
                error = "this model is deliberately unreadable";
                return false;
            }
            for (const char* const name : { "Body", "Horn", "Tail" })
            {
                contents.subAssets.push_back({ GameEngine::Assets::AssetType::Mesh, name });
            }
            if (mode == GameEngine::Assets::ImportMode::Structure)
            {
                return true;
            }
            for (std::uint32_t localId = 0; localId < 3; ++localId)
            {
                auto mesh = std::make_shared<GameEngine::Assets::MeshData>();
                mesh->id = identity.MakeResourceId(
                    GameEngine::Core::ResourceIdDomain::Mesh, localId);
                mesh->vertices.resize(3);
                mesh->indices = { 0, 1, 2 };
                contents.meshes.push_back(std::move(mesh));
            }
            return true;
        }
    };

    /// <summary>파일이 정확히 메시 하나를 담는 모델 포맷이다. 그것을 쥘 루트가 필요 없다.</summary>
    class SingleMeshImporter final : public GameEngine::Assets::IAssetImporter
    {
    public:
        [[nodiscard]] GameEngine::Assets::AssetType GetAssetType() const override
        {
            return GameEngine::Assets::AssetType::Mesh;
        }
        [[nodiscard]] bool Import(
            const std::filesystem::path& relativePath,
            std::span<const std::byte>,
            const GameEngine::Assets::ImportIdentity& identity,
            const GameEngine::Assets::ImportMode mode,
            GameEngine::Assets::ImportedContents& contents,
            std::string&) const override
        {
            contents.subAssets.push_back(
                { GameEngine::Assets::AssetType::Mesh, relativePath.stem().string() });
            if (mode == GameEngine::Assets::ImportMode::Full)
            {
                auto mesh = std::make_shared<GameEngine::Assets::MeshData>();
                mesh->id = identity.MakeResourceId(GameEngine::Core::ResourceIdDomain::Mesh, 0);
                mesh->vertices.resize(3);
                mesh->indices = { 0, 1, 2 };
                contents.meshes.push_back(std::move(mesh));
            }
            return true;
        }
    };
}

/// <summary>
/// 에셋 스캔, 결정적 매니페스트 저장·로드와 기록된 내용 해시가 다른 파일의 로드 거절을 확인한다.
/// </summary>
bool RunAssetDatabaseTests()
{
    const TestSupport::RegistryScope registries;
    TemporaryDirectory temporaryDirectory;
    const std::filesystem::path root = temporaryDirectory.GetPath();
    // 에셋마다 정체성을 준다. 매니페스트가 guid를 요구하므로, 정체성 없는 프로젝트는 패키지로
    // 나갈 수 없다 — 잘 갖춰진 프로젝트가 어떤 모습인지가 이 픽스처다.
    const auto identity = [](const char* const digits)
    {
        return std::string(R"({"guid": ")") + digits + R"("})";
    };
    if (!WriteFile(root / "AssetDatabaseTest.gameproject", "{}") ||
        !WriteFile(root / "AssetDatabaseTest.gameproject.meta",
            identity("00000000000000000000000000000001")) ||
        !WriteFile(root / "Textures" / "Albedo.PNG", "png-data") ||
        !WriteFile(root / "Textures" / "Albedo.PNG.meta",
            R"({"guid":"00000000000000000000000000000002",)"
            R"("pixelsPerUnit":64,"border":[4,5,6,7]})") ||
        !WriteFile(root / "Textures" / "Ground.jpeg", "jpeg-data") ||
        !WriteFile(root / "Textures" / "Ground.jpeg.meta",
            identity("00000000000000000000000000000003")) ||
        !WriteFile(root / "Meshes" / "Character.fbx", "fbx-data") ||
        !WriteFile(root / "Meshes" / "Character.fbx.meta",
            identity("00000000000000000000000000000004")) ||
        !WriteFile(root / "Fonts" / "Interface.otf", "font-data") ||
        !WriteFile(root / "Fonts" / "Interface.otf.meta",
            identity("00000000000000000000000000000005")) ||
        !WriteFile(root / "Audio" / "Click.wav", "audio-data") ||
        !WriteFile(root / "Audio" / "Click.wav.meta",
            identity("00000000000000000000000000000006")) ||
        !WriteFile(root / "Scenes" / "Ignored.scene", "scene-data") ||
        !WriteFile(root / "Scenes" / "Ignored.scene.meta",
            identity("00000000000000000000000000000007")))
    {
        return Expect(false, "could not create the asset database test data");
    }

    const GameEngine::Platform::DirectoryContentSource projectContent(root);
    GameEngine::Assets::AssetDatabase database;
    bool passed = Expect(database.Refresh(projectContent), "database refresh should succeed");
    passed &= Expect(database.GetAssets().size() == 7, "only supported assets should be registered");

    const auto* settings = database.FindAsset<GameEngine::Assets::ProjectSettingsAsset>(
        "AssetDatabaseTest.gameproject");
    const auto* png = database.FindAsset<GameEngine::Assets::Sprite>("Textures/Albedo.PNG");
    const auto* jpeg = database.FindAsset<GameEngine::Assets::Sprite>("Textures/Ground.jpeg");
    const auto* fbx = database.FindAsset<GameEngine::Assets::Mesh>("Meshes/Character.fbx");
    const auto* scene = database.FindAsset<GameEngine::Assets::SceneAsset>("Scenes/Ignored.scene");
    const auto* font = database.FindAsset<GameEngine::Assets::Font>("Fonts/Interface.otf");
    const auto* audio = database.FindAsset<GameEngine::Assets::AudioClip>("Audio/Click.wav");
    passed &= Expect(
        settings,
        ".gameproject should be a ProjectSettings asset");
    passed &= Expect(
        png && png->GetPixelsPerUnit() == 64.0f && png->GetBorder().left == 4.0f &&
            png->GetBorder().top == 5.0f && png->GetBorder().right == 6.0f &&
            png->GetBorder().bottom == 7.0f,
        "PNG should be a Sprite asset with import metadata");
    passed &= Expect(
        jpeg && !jpeg->HasBorder(),
        "JPEG should be a Sprite asset with default metadata");
    passed &= Expect(
        fbx,
        "FBX should be a Mesh asset");
    passed &= Expect(
        scene,
        "scene files should be Scene assets");
    passed &= Expect(
        font,
        "OTF should be a Font asset");
    passed &= Expect(audio, "WAV should be an AudioClip asset");

    // 종류별 선택지는 경로 순이고, 임포터가 읽지 못한 파일은 선택지를 내지 않는다 — 이 FBX는
    // 가짜 바이트라 메시가 하나도 없다.
    const std::vector<GameEngine::Assets::AssetChoice> spriteChoices =
        GameEngine::Assets::CollectAssetChoices(database, GameEngine::Assets::AssetType::Sprite);
    passed &= Expect(
        spriteChoices.size() == 2 && spriteChoices[0].label == "Textures/Albedo.PNG" &&
            spriteChoices[0].reference.ToString() == "Textures/Albedo.PNG" &&
            spriteChoices[1].label == "Textures/Ground.jpeg" &&
            database.FindAsset(spriteChoices[0].reference) == png,
        "sprite choices should list every sprite in path order with a resolvable reference");
    passed &= Expect(
        GameEngine::Assets::CollectAssetChoices(database, GameEngine::Assets::AssetType::AudioClip)
                .size() == 1,
        "audio choices should list the one clip");
    passed &= Expect(
        GameEngine::Assets::CollectAssetChoices(database, GameEngine::Assets::AssetType::Mesh)
            .empty(),
        "an unreadable model should offer no mesh to choose");
    passed &= Expect(
        GameEngine::Assets::AssetDatabase::ParseAssetTypeName(
            GameEngine::Assets::AssetDatabase::GetAssetTypeName(
                GameEngine::Assets::AssetType::Mesh)) == GameEngine::Assets::AssetType::Mesh &&
            !GameEngine::Assets::AssetDatabase::ParseAssetTypeName("Nope").has_value(),
        "asset type names should parse back to the type they were written from");

    // 사이드카는 이름으로 그 소유 에셋에 닿는다 — 파일이 있든 없든. 종류를 가리지 않는다:
    // 등록되는 모든 에셋이 <원본>.meta 하나를 갖는다.
    passed &= Expect(
        database.FindSidecarOwner("Textures/Albedo.PNG.meta") == png &&
            database.FindSidecarOwner("textures/albedo.png.meta") == png &&
            database.FindSidecarOwner("Textures/Ground.jpeg.meta") == jpeg &&
            database.FindSidecarOwner("Meshes/Character.fbx.meta") == fbx &&
            database.FindSidecarOwner("Audio/Click.wav.meta") == audio &&
            database.FindSidecarOwner("Textures/Albedo.PNG") == nullptr,
        "every asset should own the .meta named after it, and nothing else");
    // 옛 이름은 그것을 쓰던 형식의 것만이다. 메시에는 `.sprite.json`이 있었던 적이 없다.
    passed &= Expect(
        database.FindSidecarOwner("Textures/Albedo.PNG.sprite.json") == png &&
            database.FindSidecarOwner("Meshes/Character.fbx.sprite.json") == nullptr,
        "the legacy name should belong only to the kind that used it");

    // 에셋 참조를 받는 자리가 무엇을 받는지: 종류를 말하는 자리는 그 종류만, 말하지 않는 자리는
    // 해석되기만 하면 받는다. 인스펙터의 목록 거르기와 끌어 놓기가 같은 이 규칙을 쓴다.
    //
    // 참조는 에셋 자신에게 물어 만든다. 손으로 적은 경로는 임포터가 읽지 못한 파일을 가리킬 수
    // 있고 — 이 픽스처의 FBX가 그렇다 — 그러면 종류가 아니라 해석 실패를 재게 된다.
    using GameEngine::Assets::AssetReferenceMatchesType;
    using GameEngine::Assets::AssetType;
    using GameEngine::Assets::MakeAssetReference;
    if (png && scene)
    {
        const GameEngine::Assets::AssetReference spriteRef = MakeAssetReference(*png);
        const GameEngine::Assets::AssetReference sceneRef = MakeAssetReference(*scene);
        passed &= Expect(
            AssetReferenceMatchesType(database, spriteRef, AssetType::Sprite) &&
                !AssetReferenceMatchesType(database, spriteRef, AssetType::Mesh) &&
                !AssetReferenceMatchesType(database, spriteRef, AssetType::AudioClip),
            "a sprite reference should match a sprite slot and no other kind");
        passed &= Expect(
            AssetReferenceMatchesType(database, sceneRef, AssetType::Scene) &&
                !AssetReferenceMatchesType(database, sceneRef, AssetType::Sprite),
            "a scene reference should match a scene slot and not a sprite one");
        passed &= Expect(
            AssetReferenceMatchesType(database, spriteRef, std::nullopt) &&
                AssetReferenceMatchesType(database, sceneRef, std::nullopt),
            "a slot that names no kind should take any resolvable reference");
    }
    // 해석되지 않는 참조는 어느 자리도 받지 않는다. 종류를 묻지 않는 자리도 마찬가지다 — 빈
    // 참조와 오타 난 참조는 값이 아니라 실수다.
    passed &= Expect(
        !AssetReferenceMatchesType(
            database, GameEngine::Assets::AssetReference("Textures/Gone.png"), std::nullopt) &&
            !AssetReferenceMatchesType(
                database, GameEngine::Assets::AssetReference("Meshes/Character.fbx"),
                AssetType::Mesh) &&
            !AssetReferenceMatchesType(
                database, GameEngine::Assets::AssetReference{}, std::nullopt),
        "a reference that resolves to nothing should match no slot at all");
    passed &= Expect(
        png && database.FindAsset(png->GetId()) == png,
        "asset lookup by deterministic ID should return the same record");

    const std::filesystem::path firstManifest = root / "AssetDatabase.json";
    const std::filesystem::path secondManifest = root / "AssetDatabase-copy.json";
    passed &= Expect(database.SaveManifest(firstManifest), "manifest save should succeed");
    passed &= Expect(database.SaveManifest(secondManifest), "second manifest save should succeed");

    std::ifstream firstStream(firstManifest, std::ios::binary);
    std::ifstream secondStream(secondManifest, std::ios::binary);
    const std::string firstContents{
        std::istreambuf_iterator<char>(firstStream), std::istreambuf_iterator<char>()
    };
    const std::string secondContents{
        std::istreambuf_iterator<char>(secondStream), std::istreambuf_iterator<char>()
    };
    passed &= Expect(firstContents == secondContents, "manifest output should be deterministic");

    std::string oldFormatContents = firstContents;
    const std::string formatVersion = "\"formatVersion\": 6";
    const std::size_t formatVersionOffset = oldFormatContents.find(formatVersion);
    passed &= Expect(formatVersionOffset != std::string::npos, "manifest should use format version 6");
    if (formatVersionOffset != std::string::npos)
    {
        oldFormatContents[formatVersionOffset + formatVersion.size() - 1] = '1';
        const std::filesystem::path oldFormatManifest = root / "AssetDatabase-v1.json";
        passed &= Expect(
            WriteFile(oldFormatManifest, oldFormatContents),
            "write an old-format manifest");
        GameEngine::Assets::AssetDatabase oldFormatDatabase;
        passed &= Expect(
            !oldFormatDatabase.LoadManifest(projectContent, "AssetDatabase-v1.json"),
            "an old manifest without Font asset semantics should be rejected");
    }

    GameEngine::Assets::AssetDatabase loadedDatabase;
    passed &= Expect(
        loadedDatabase.LoadManifest(projectContent, firstManifest.filename()),
        "saved manifest should load successfully");
    passed &= Expect(
        loadedDatabase.GetAssets().size() == database.GetAssets().size(),
        "loaded manifest should contain every registered asset");
    const auto* loadedPng =
        loadedDatabase.FindAsset<GameEngine::Assets::Sprite>("Textures/Albedo.PNG");
    passed &= Expect(
        loadedPng && png && loadedPng->GetId() == png->GetId() &&
            loadedPng->GetContentHash() == png->GetContentHash() &&
            loadedPng->GetBorder().right == png->GetBorder().right,
        "loaded asset identity and content hash should be preserved");

    // A stale hash fails the load of the changed file, before its bytes can be handed out.
    // Opening the manifest must not read every payload or fault the whole pack into memory.
    passed &= Expect(
        WriteFile(root / "Textures" / "Albedo.PNG", "bad-data"),
        "test texture mutation should succeed");
    GameEngine::Assets::AssetDatabase staleDatabase;
    passed &= Expect(
        staleDatabase.LoadManifest(projectContent, firstManifest.filename()),
        "a manifest should open without reading every file");
    passed &= Expect(
        staleDatabase.LoadTexture(
            GameEngine::Assets::AssetReference("Textures/Albedo.PNG")) == nullptr,
        "a file whose bytes no longer match its record should refuse to load");

    const std::filesystem::path invalidRoot = root / "MissingSettings";
    std::filesystem::create_directories(invalidRoot);
    const GameEngine::Platform::DirectoryContentSource invalidContent(invalidRoot);
    GameEngine::Assets::AssetDatabase invalidDatabase;
    passed &= Expect(
        !invalidDatabase.Refresh(invalidContent),
        "a project without a .gameproject file should be rejected");

    // 깨진 사이드카는 스프라이트를 기본값으로 처리하고 프로젝트 열기를 막지 않는다.
    // 이 refresh는 사이드카 문제를 알리는 의도된 오류 로그를 남긴다.
    const std::filesystem::path sidecarRoot = root / "BrokenSidecar";
    std::filesystem::create_directories(sidecarRoot);
    passed &= Expect(
        WriteFile(sidecarRoot / "Sidecar.gameproject", "{}") &&
            WriteFile(sidecarRoot / "Art.png", "png-data") &&
            WriteFile(sidecarRoot / "Art.png.meta", R"({"pixelsPerUnit": -3})"),
        "the broken sidecar test data should be written");
    const GameEngine::Platform::DirectoryContentSource sidecarContent(sidecarRoot);
    GameEngine::Assets::AssetDatabase sidecarDatabase;
    passed &= Expect(
        sidecarDatabase.Refresh(sidecarContent),
        "a broken sidecar should not stop a project from opening");
    const auto* const degraded = sidecarDatabase.FindAsset<GameEngine::Assets::Sprite>("Art.png");
    passed &= Expect(
        degraded && degraded->GetPixelsPerUnit() == 100.0f && !degraded->HasBorder(),
        "a sprite with a broken sidecar should be registered with default metadata");

    // 사이드카가 붙은 스프라이트를 단건으로 등록하는 길이다. Refresh는 이미 열거한 파일 목록에
    // 사이드카가 있는지 묻지만, 파일 하나만 다시 등록할 때는 그 목록이 없어 원본에 직접 묻는다.
    // 두 길이 같은 답을 내야 하므로, 목록 없는 쪽도 여기서 고정한다.
    const std::filesystem::path singleRoot = root / "SingleRegister";
    std::filesystem::create_directories(singleRoot);
    passed &= Expect(
        WriteFile(singleRoot / "Single.gameproject", "{}") &&
            WriteFile(singleRoot / "Tile.png", "png-data") &&
            WriteFile(singleRoot / "Tile.png.meta",
                R"({"pixelsPerUnit": 32.0, "border": [2.0, 2.0, 2.0, 2.0]})"),
        "the single-registration test data should be written");
    const GameEngine::Platform::DirectoryContentSource singleContent(singleRoot);
    GameEngine::Assets::AssetDatabase singleDatabase;
    passed &= Expect(
        singleDatabase.Refresh(singleContent), "the single-registration project should open");
    passed &= Expect(
        WriteFile(singleRoot / "Tile.png", "png-data-changed"),
        "the sprite should be rewritten before it is registered again");
    passed &= Expect(
        singleDatabase.RegisterAsset(singleRoot / "Tile.png"),
        "registering a single sprite again should succeed");
    const auto* const reregistered =
        singleDatabase.FindAsset<GameEngine::Assets::Sprite>("Tile.png");
    passed &= Expect(
        reregistered && reregistered->GetPixelsPerUnit() == 32.0f &&
            reregistered->GetBorder().left == 2.0f,
        "a single registration should read the sidecar the same way a scan does");

    return passed;
}

/// <summary>
/// 파일은 에셋을 담고, 몇 개이며 무엇이라 불리는지는 그 확장자의 임포터가 말한다. 에셋이 곧
/// 파일이고 종류가 확장자 switch에서 나오면, 여러 메시를 담은 모델에 대해서는 어느 질문에도
/// 답할 수 있는 것이 없다.
/// </summary>
bool RunAssetImporterTests()
{
    const TestSupport::RegistryScope registries;
    using GameEngine::Assets::AssetDatabase;
    using GameEngine::Assets::AssetImporterRegistry;
    using GameEngine::Assets::AssetReference;
    using GameEngine::Assets::AssetType;
    using GameEngine::Assets::SubAsset;

    static const FakeModelImporter fakeModelImporter;
    const bool registered = AssetImporterRegistry::Register(".testmodel", fakeModelImporter);
    // Replacing a format silently is what this refuses: a second importer for one extension
    // would change what every existing file of that type means.
    const bool refusesDuplicate =
        !AssetImporterRegistry::Register(".testmodel", fakeModelImporter) &&
        !AssetImporterRegistry::Register(".fbx", fakeModelImporter);
    const bool refusesMalformed =
        !AssetImporterRegistry::Register("testmodel", fakeModelImporter) &&
        !AssetImporterRegistry::Register(".", fakeModelImporter);

    TemporaryDirectory temporaryDirectory("importer");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const bool wrote =
        WriteFile(root / "ImporterTest.gameproject", "{}") &&
        WriteFile(root / "ImporterTest.gameproject.meta",
            R"({"guid": "000000000000000000000000000000a1"})") &&
        WriteFile(root / "Models" / "Creature.testmodel", "model-data") &&
        WriteFile(root / "Models" / "Creature.testmodel.meta",
            R"({"guid": "000000000000000000000000000000a2"})") &&
        WriteFile(root / "Models" / "Broken.testmodel", "not-a-model") &&
        WriteFile(root / "Models" / "Broken.testmodel.meta",
            R"({"guid": "000000000000000000000000000000a3"})") &&
        WriteFile(root / "Textures" / "Albedo.png", "png-data") &&
        WriteFile(root / "Textures" / "Albedo.png.meta",
            R"({"guid": "000000000000000000000000000000a4"})");

    AssetDatabase database;
    const GameEngine::Platform::DirectoryContentSource projectContent(root);
    const bool refreshed = wrote && database.Refresh(projectContent);

    const GameEngine::Assets::Asset* const model =
        database.FindAsset("Models/Creature.testmodel");
    const std::vector<SubAsset> modelSubAssets =
        model ? model->GetSubAssets() : std::vector<SubAsset>{};
    const bool importedSubAssets =
        modelSubAssets.size() == 3 &&
        modelSubAssets[0].name == "Body" && modelSubAssets[1].name == "Horn" &&
        modelSubAssets[2].name == "Tail" &&
        std::ranges::all_of(
            modelSubAssets,
            [](const SubAsset& subAsset) { return subAsset.type == AssetType::Mesh; });

    // A file that holds one thing still holds it as a sub-asset, named after the file, so no
    // caller has to special-case the single-asset formats.
    const GameEngine::Assets::Asset* const texture = database.FindAsset("Textures/Albedo.png");
    const bool singleAssetFile =
        texture && texture->GetSubAssets().size() == 1 &&
        texture->GetSubAssets().front().name == "Albedo" &&
        texture->GetSubAssets().front().type == AssetType::Sprite;

    // An unreadable file is still registered. One broken model must not stop a project from
    // opening, and the failure surfaces where a reference into it is used.
    const GameEngine::Assets::Asset* const broken =
        database.FindAsset("Models/Broken.testmodel");
    const bool brokenFileRegistered = broken && broken->GetSubAssets().empty();

    // The point of all of it: a reference names one asset inside a file, and a reference to an
    // asset the file does not hold resolves to nothing.
    const bool resolvesSubAsset =
        database.FindAsset(AssetReference("Models/Creature.testmodel", 2)) == model &&
        database.FindAsset(AssetReference("Models/Creature.testmodel", 3)) == nullptr &&
        database.FindAsset(AssetReference("Models/Broken.testmodel", 0)) == nullptr &&
        database.FindAsset(AssetReference("Textures/Albedo.png")) == texture;

    // Import results survive the manifest, so a deployed player does not reimport a model just
    // to learn how many meshes it holds.
    const std::filesystem::path manifestPath = "Imported.json";
    AssetDatabase loaded;
    const bool roundTripped = database.SaveManifest(root / manifestPath) &&
        loaded.LoadManifest(projectContent, manifestPath);
    const GameEngine::Assets::Asset* const loadedModel =
        loaded.FindAsset("Models/Creature.testmodel");
    const bool manifestKeptSubAssets =
        roundTripped && loadedModel && loadedModel->GetSubAssets().size() == 3 &&
        loadedModel->GetSubAssets()[1].name == "Horn";

    // Opening a manifest checks file existence; content hashes are verified on first load.
    // A changed file can therefore open with its project but must fail to load when its bytes do not match the manifest.
    const bool editedAfterPackaging =
        WriteFile(root / "Models" / "Creature.testmodel", "model-data-edited");
    AssetDatabase stale;
    const bool editedManifestStillOpens =
        editedAfterPackaging && stale.LoadManifest(projectContent, manifestPath);
    const bool editedFileRefusesToLoad = editedManifestStillOpens &&
        stale.LoadMesh(AssetReference("Models/Creature.testmodel", 1)) == nullptr &&
        stale.GetLoadedPayloadCount() == 0;

    const bool unregistered = AssetImporterRegistry::Unregister(".testmodel") &&
        !AssetImporterRegistry::Unregister(".testmodel") &&
        !AssetImporterRegistry::IsSupportedExtension(".testmodel");

    // With nothing importing the extension, those files stop being assets at all.
    AssetDatabase withoutImporter;
    const bool forgotten = withoutImporter.Refresh(projectContent) &&
        withoutImporter.FindAsset("Models/Creature.testmodel") == nullptr;

    return Expect(registered, "an extension with no importer should accept one") &&
        Expect(refusesDuplicate, "an already imported extension should refuse a second importer") &&
        Expect(refusesMalformed, "text that is not an extension should be refused") &&
        Expect(refreshed, "a project holding a registered format should refresh") &&
        Expect(importedSubAssets, "a file should hold the assets its importer reports") &&
        Expect(singleAssetFile, "a single-asset file should hold one asset named after it") &&
        Expect(brokenFileRegistered, "a file that cannot be imported should hold no assets") &&
        Expect(resolvesSubAsset, "a reference should resolve only to an asset the file holds") &&
        Expect(manifestKeptSubAssets, "a manifest should carry what the importer found") &&
        Expect(editedManifestStillOpens, "a manifest should open without reading every file") &&
        Expect(editedFileRefusesToLoad, "a file edited after packaging should refuse to load") &&
        Expect(unregistered, "an importer should be removable") &&
        Expect(forgotten, "a file whose format has no importer is not an asset");
}

/// <summary>
/// 프로젝트가 무엇을 담고 있는지와 지금 메모리에 무엇이 있는지는 다른 질문이다.
///
/// 스캔이 곧 로드이면 프로젝트가 담은 모든 것이 열리는 순간부터 닫힐 때까지 상주한다 —
/// 디코딩은 이미지를 6~50배로 불리니, 비싼 쪽이 그것이다. 스캔은 각 파일이 무엇을 담는지만
/// 기록하고, 페이로드는 무언가 요청할 때 도착해 아무도 쓰지 않을 때 떠난다.
/// </summary>
bool RunAssetResidencyTests()
{
    const TestSupport::RegistryScope registries;
    using GameEngine::Assets::AssetDatabase;
    using GameEngine::Assets::AssetImporterRegistry;
    using GameEngine::Assets::AssetReference;

    static const FakeModelImporter fakeModelImporter;
    const bool registered = AssetImporterRegistry::Register(".testmodel", fakeModelImporter);

    TemporaryDirectory temporaryDirectory("residency");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const bool wrote =
        WriteFile(root / "ResidencyTest.gameproject", "{}") &&
        WriteFile(root / "Models" / "Creature.testmodel", "model-data") &&
        WriteFile(root / "Models" / "Rock.testmodel", "model-data");

    const GameEngine::Platform::DirectoryContentSource projectContent(root);
    AssetDatabase database;
    const bool refreshed = wrote && database.Refresh(projectContent);

    // A scan knows what the files hold and has loaded none of it.
    const GameEngine::Assets::Asset* const creature =
        database.FindAsset("Models/Creature.testmodel");
    const bool scanKnowsStructure = creature && creature->GetSubAssets().size() == 3 &&
        creature->GetSubAssets()[1].name == "Horn";
    const bool scanLoadsNothing = database.GetLoadedPayloadCount() == 0;

    std::shared_ptr<const GameEngine::Assets::MeshData> horn =
        database.LoadMesh(AssetReference("Models/Creature.testmodel", 1));
    const bool loadsOnDemand = horn != nullptr && database.GetLoadedPayloadCount() == 1;

    // A model is parsed once and yields every mesh in it, so its siblings are already there.
    std::shared_ptr<const GameEngine::Assets::MeshData> body =
        database.LoadMesh(AssetReference("Models/Creature.testmodel", 0));
    const bool siblingsComeTogether = body != nullptr && body != horn &&
        database.GetLoadedPayloadCount() == 1;

    // Asking twice hands back the same object rather than parsing again.
    const bool sameOnSecondAsk =
        database.LoadMesh(AssetReference("Models/Creature.testmodel", 1)) == horn;

    // Ids distinguish the meshes inside one file, and distinguish kinds, so any cache may key
    // on one.
    const bool idsAreDistinct = horn && body && horn->id != body->id &&
        GameEngine::Core::GetResourceIdDomain(horn->id) ==
            GameEngine::Core::ResourceIdDomain::Mesh;

    // Nothing else was touched by loading one file.
    const bool loadsOnlyWhatWasAsked =
        database.FindAsset("Models/Rock.testmodel") != nullptr &&
        database.GetLoadedPayloadCount() == 1;

    // A second file, loaded and then let go of, so nothing outside the database holds it.
    const bool bothLoaded =
        database.LoadMesh(AssetReference("Models/Rock.testmodel", 0)) != nullptr &&
        database.GetLoadedPayloadCount() == 2;

    // A reference count cannot say which assets are wanted: this database holds the only
    // persistent reference to a payload, so every loaded asset looks unused from in here —
    // including the ones a running scene draws every frame. Which are wanted is the caller's
    // answer, and here the caller wants only the creature.
    const std::unordered_set<GameEngine::Assets::AssetKey> keepCreature{ creature->GetId() };
    database.UnloadUnreferenced(keepCreature);
    const bool keepsWhatIsReferenced = database.GetLoadedPayloadCount() == 1 && horn->IsValid();

    // Now nothing is wanted, but the creature's meshes are still held here — which is what a
    // frame in flight and a backend's resolved resource look like. It survives and is collected
    // by a later sweep, because freeing it out from under either would be a crash.
    database.UnloadUnreferenced({});
    const bool keepsWhatIsHeldElsewhere = database.GetLoadedPayloadCount() == 1;

    horn.reset();
    body.reset();
    database.UnloadUnreferenced({});
    const bool releasesWhatIsNot = database.GetLoadedPayloadCount() == 0;

    // And it can be loaded again afterwards, which is what makes unloading safe to do at all.
    const bool reloads =
        database.LoadMesh(AssetReference("Models/Creature.testmodel", 1)) != nullptr &&
        database.GetLoadedPayloadCount() == 1;

    // A changed file keeps its asset id — the id is a path hash — so re-registering must drop
    // the resident payload. Without that, every later load serves the stale contents, and the
    // fresh import is provable through the mesh id, which folds in the content hash.
    const std::shared_ptr<const GameEngine::Assets::MeshData> beforeEdit =
        database.LoadMesh(AssetReference("Models/Creature.testmodel", 1));
    const std::uint64_t idBeforeEdit = beforeEdit ? beforeEdit->id : 0;
    const bool reregistered =
        WriteFile(root / "Models" / "Creature.testmodel", "model-data-edited") &&
        database.RegisterAsset("Models/Creature.testmodel");
    const bool editDropsResidentPayload =
        reregistered && database.GetLoadedPayloadCount() == 0;
    const std::shared_ptr<const GameEngine::Assets::MeshData> afterEdit =
        database.LoadMesh(AssetReference("Models/Creature.testmodel", 1));
    const bool editReimports =
        afterEdit && idBeforeEdit != 0 && afterEdit->id != idBeforeEdit;


    return Expect(registered && refreshed, "a project with a model format should refresh") &&
        Expect(scanKnowsStructure, "a scan should record what each file holds") &&
        Expect(scanLoadsNothing, "a scan should load none of it") &&
        Expect(loadsOnDemand, "asking for a mesh should load it") &&
        Expect(siblingsComeTogether, "loading one mesh of a file should yield its siblings") &&
        Expect(sameOnSecondAsk, "asking twice should not parse twice") &&
        Expect(idsAreDistinct, "each mesh in a file should have its own id") &&
        Expect(loadsOnlyWhatWasAsked, "loading one file should not load another") &&
        Expect(bothLoaded, "two files should load independently") &&
        Expect(keepsWhatIsReferenced, "unloading should keep what the caller says is referenced") &&
        Expect(
            keepsWhatIsHeldElsewhere,
            "unloading should keep what something else is still holding") &&
        Expect(releasesWhatIsNot, "unloading should release what nothing wants or holds") &&
        Expect(reloads, "an unloaded asset should load again when asked for") &&
        Expect(editDropsResidentPayload, "re-registering a changed file should drop its resident payload") &&
        Expect(editReimports, "a load after re-registration should serve the file's new contents");
}

/// <summary>
/// 모델 파일은 geometry마다 메시 에셋 하나를 담고, MeshRenderer는 정확히 메시 하나를 그린다.
/// 따라서 그런 파일을 장면에 넣는다는 것은 공유 루트 아래 메시마다 객체 하나라는 뜻이다 —
/// 아니면 다섯 부품짜리 모델은 사람이 손으로 만들어야 하는 객체 다섯이고, 각각에 올바른 로컬
/// id를 쳐 넣어야 한다.
/// </summary>
bool RunModelInstantiationTests()
{
    const TestSupport::RegistryScope registries;
    using GameEngine::Assets::AssetDatabase;
    using GameEngine::Assets::AssetImporterRegistry;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::MeshRenderer;
    using GameEngine::Runtime::ObjectRegistry;
    using GameEngine::Runtime::Scene;

    static const FakeModelImporter fakeModelImporter;
    static const SingleMeshImporter singleMeshImporter;
    const bool registered =
        AssetImporterRegistry::Register(".testmodel", fakeModelImporter) &&
        AssetImporterRegistry::Register(".testsolo", singleMeshImporter);

    TemporaryDirectory temporaryDirectory("instantiate");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const bool wrote =
        WriteFile(root / "InstantiateTest.gameproject", "{}") &&
        WriteFile(root / "Models" / "Creature.testmodel", "model-data") &&
        WriteFile(root / "Models" / "Rock.testsolo", "model-data");

    AssetDatabase database;
    const GameEngine::Platform::DirectoryContentSource projectContent(root);
    const bool refreshed = wrote && database.Refresh(projectContent);

    ObjectRegistry objectRegistry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext runtimeContext(objectRegistry, input);
    Scene scene(runtimeContext, "Instantiation");

    GameObject* const model = refreshed
        ? GameEngine::Runtime::InstantiateModel(scene, database, "Models/Creature.testmodel")
        : nullptr;
    const std::vector<GameEngine::Runtime::Transform*> parts =
        model ? model->GetTransform().GetChildren()
              : std::vector<GameEngine::Runtime::Transform*>{};

    const bool builtHierarchy = model && model->GetName() == "Creature" && parts.size() == 3 &&
        // The root only carries the model; the meshes are on the children.
        model->GetComponent<MeshRenderer>() == nullptr;

    bool partsAreCorrect = builtHierarchy;
    const char* const expectedNames[] = { "Body", "Horn", "Tail" };
    for (std::size_t index = 0; partsAreCorrect && index < parts.size(); ++index)
    {
        const GameObject* const part = parts[index]->GetGameObject();
        const MeshRenderer* const renderer = part ? part->GetComponent<MeshRenderer>() : nullptr;
        partsAreCorrect =
            renderer != nullptr &&
            part->GetName() == expectedNames[index] &&
            renderer->GetMesh().GetLocalId() == static_cast<std::uint32_t>(index) &&
            renderer->GetMesh().GetPath() == std::filesystem::path("Models/Creature.testmodel");
    }

    // The importer bakes each mesh's placement inside the file into its vertices, so a child
    // that also carried that placement would apply it twice.
    bool partsAreUnmoved = builtHierarchy;
    for (GameEngine::Runtime::Transform* const part : parts)
    {
        const GameEngine::Math::Vector3 position = part->GetPosition();
        const GameEngine::Math::Vector3 scale = part->GetScale();
        partsAreUnmoved = partsAreUnmoved &&
            position.GetX() == 0.0f && position.GetY() == 0.0f && position.GetZ() == 0.0f &&
            scale.GetX() == 1.0f && scale.GetY() == 1.0f && scale.GetZ() == 1.0f;
    }

    // A file with one mesh gets no wrapper: a root whose only job is to hold one child is
    // clutter in the hierarchy, and Unity does not add one either.
    GameObject* const solo = refreshed
        ? GameEngine::Runtime::InstantiateModel(scene, database, "Models/Rock.testsolo")
        : nullptr;
    const bool singleMeshIsOneObject = solo && solo->GetTransform().GetChildren().empty() &&
        solo->GetComponent<MeshRenderer>() != nullptr &&
        solo->GetComponent<MeshRenderer>()->GetMesh().GetLocalId() == 0;

    // Nothing is left behind when there is nothing to instantiate.
    const std::size_t objectsBefore = scene.GetGameObjects().size();
    const bool refusesUnknown =
        GameEngine::Runtime::InstantiateModel(scene, database, "Models/Missing.testmodel") ==
            nullptr &&
        scene.GetGameObjects().size() == objectsBefore;


    return Expect(registered && refreshed, "a project with model formats should refresh") &&
        Expect(builtHierarchy, "a multi-mesh model should become a root with a child per mesh") &&
        Expect(partsAreCorrect, "each part should draw its own mesh out of the same file") &&
        Expect(partsAreUnmoved, "a part should not carry a placement its vertices already have") &&
        Expect(singleMeshIsOneObject, "a single-mesh model should become one object") &&
        Expect(refusesUnknown, "a model that is not an asset should add nothing to the scene");
}

/// <summary>
/// 에셋 참조는 파일을, 그리고 선택적으로 그 안의 무언가를 가리킨다.
///
/// 중요한 것은 호환 케이스다: sub-asset이 생기기 전에 쓰인 모든 참조는 맨 경로이고, 그것들
/// 전부가 파일의 대표 에셋을 계속 뜻해야 한다. 고정해 둘 가치가 있는 다른 케이스는 자기
/// 이름에 '#'을 담은 경로인데, 그것을 sub-asset 한정자로 오해해서는 안 된다.
/// </summary>
bool RunAssetReferenceTests()
{
    using GameEngine::Assets::AssetReference;

    const AssetReference bare = AssetReference::Parse("Meshes/Model.fbx");
    const AssetReference sub = AssetReference::Parse("Meshes/Model.fbx#2");
    const AssetReference hashInName = AssetReference::Parse("Meshes/Model#1 copy.fbx");
    const AssetReference trailingHash = AssetReference::Parse("Meshes/Model.fbx#");
    const AssetReference empty = AssetReference::Parse("");

    const bool compatible =
        bare.IsValid() && bare.IsMainAsset() &&
        bare.GetPath() == std::filesystem::path("Meshes/Model.fbx");
    const bool addressed =
        sub.IsValid() && !sub.IsMainAsset() && sub.GetLocalId() == 2 &&
        sub.GetPath() == std::filesystem::path("Meshes/Model.fbx");
    const bool literalHash =
        hashInName.IsMainAsset() &&
        hashInName.GetPath() == std::filesystem::path("Meshes/Model#1 copy.fbx") &&
        trailingHash.IsMainAsset() &&
        trailingHash.GetPath() == std::filesystem::path("Meshes/Model.fbx#");

    // A main asset writes no suffix, so a scene round-trips to the text it was written with.
    const bool roundTrips =
        bare.ToString() == "Meshes/Model.fbx" &&
        sub.ToString() == "Meshes/Model.fbx#2" &&
        AssetReference::Parse(sub.ToString()) == sub;

    // 참조가 실제 프로젝트에서 해석되는지도 확인한다. 잘못된 참조와 일부러 비운 참조를
    // 구분해야 사용자가 고칠 값을 알 수 있다.
    TemporaryDirectory referenceDirectory;
    const std::filesystem::path referenceRoot = referenceDirectory.GetPath();
    bool classifies = false;
    if (WriteFile(referenceRoot / "ReferenceTest.gameproject", "{}") &&
        WriteFile(referenceRoot / "Resources" / "Ground.png", "png-data"))
    {
        const GameEngine::Platform::DirectoryContentSource referenceContent(referenceRoot);
        GameEngine::Assets::AssetDatabase referenceDatabase;
        using GameEngine::Assets::AssetReferenceStatus;
        using GameEngine::Assets::ClassifyAssetReference;
        classifies = referenceDatabase.Refresh(referenceContent) &&
            // 있는 것을 가리키면 해석된다.
            ClassifyAssetReference(
                referenceDatabase, AssetReference::Parse("Resources/Ground.png")) ==
                AssetReferenceStatus::Resolved &&
            // 오타는 오류다 — 한 글자만 달라도 가리키는 것이 없다.
            ClassifyAssetReference(
                referenceDatabase, AssetReference::Parse("Resources/Ground.pgn")) ==
                AssetReferenceStatus::Missing &&
            // 빈 참조는 오류가 아니라 결정이다.
            ClassifyAssetReference(referenceDatabase, AssetReference::Parse("")) ==
                AssetReferenceStatus::Empty &&
            // 파일은 있지만 그 안에 그런 자리가 없는 것도 해석 실패다.
            ClassifyAssetReference(
                referenceDatabase, AssetReference::Parse("Resources/Ground.png#7")) ==
                AssetReferenceStatus::Missing;
    }

    // 브라우저가 내놓는 참조는 사람이 옮겨 적은 경로가 아니라 에셋 자신이 답한 이름이다. 그
    // 이름이 같은 데이터베이스에서 반드시 해석되어야, 복사해 붙여넣는 것이 오타를 없애는 길이
    // 된다. 사용자의 실제 콘텐츠에 공백이 든 파일 이름이 있으므로 그것도 함께 본다.
    bool copiedReferencesResolve = false;
    // 임포트되지 않는 파일은 참조로도 해석되지 않는 것이 옳다(그 파일 안에 에셋이 없다). 여기서
    // 보는 것은 참조 문자열의 모양이므로, 이 테스트가 만들 수 있는 진짜 에셋들로만 본다.
    if (WriteFile(referenceRoot / "Resources" / "TX Tileset Ground.png", "png-data"))
    {
        const GameEngine::Platform::DirectoryContentSource copyContent(referenceRoot);
        GameEngine::Assets::AssetDatabase copyDatabase;
        copiedReferencesResolve = copyDatabase.Refresh(copyContent) &&
            !copyDatabase.GetAssets().empty();
        bool sawSpacedName = false;
        for (const std::unique_ptr<GameEngine::Assets::Asset>& asset : copyDatabase.GetAssets())
        {
            // 복사되는 것은 참조의 문자열이므로, 문자열을 거쳐 되돌아온 참조로 판정한다.
            const std::string copied =
                GameEngine::Assets::MakeAssetReference(*asset).ToString();
            sawSpacedName = sawSpacedName || copied.find("TX Tileset Ground.png") != std::string::npos;
            copiedReferencesResolve = copiedReferencesResolve &&
                GameEngine::Assets::ClassifyAssetReference(
                    copyDatabase, AssetReference::Parse(copied)) ==
                    GameEngine::Assets::AssetReferenceStatus::Resolved;
        }
        copiedReferencesResolve = copiedReferencesResolve && sawSpacedName;
    }

    return Expect(compatible, "a bare path should name a file's main asset") &&
        Expect(addressed, "a suffixed path should name an asset inside the file") &&
        Expect(literalHash, "a '#' that is not a sub-asset qualifier should stay in the path") &&
        Expect(roundTrips, "a reference should round-trip through its scene-file form") &&
        Expect(!empty.IsValid(), "an empty reference should name nothing") &&
        Expect(
            classifies,
            "a reference should be reported as resolved, missing, or deliberately empty") &&
        Expect(
            copiedReferencesResolve,
            "a reference taken from an asset should resolve in the database it came from");
}

/// <summary>
/// 메시, 텍스처, 래스터화된 텍스트 이미지는 결코 id를 공유하지 않는다.
///
/// 프론트엔드의 각 캐시는 저마다 1부터 세고, 백엔드는 id를 캐시 키로 쓴다. D3D12의 텍스처
/// 바인딩 캐시는 머티리얼과 텍스트가 공유하므로, 첫 머티리얼과 첫 텍스트 줄이 거기서 충돌해
/// 서로의 텍스처로 resolve됐을 것이다. 어떤 캐시든 id를 키로 삼을 수 있도록 종류가 id의
/// 일부다.

/// <summary>
/// 실제 샘플 콘텐츠가 쓰는 시트 모양들을 고정한다. 이 프로젝트가 담은 시트는 교과서적인 정사각
/// 격자가 아니다: 타일셋은 256칸을 다 쓰고, 상자 시트는 8×8 격자의 앞 32칸만 쓰며, 두 불꽃
/// 시트는 셀 크기가 정수 픽셀로 떨어지지 않는다(200/6, 128/6). 눈으로는 보이지 않고 숫자로만
/// 보이는 것들이라 — 마지막 프레임이 이미지 밖을 집는지, 부분만 쓰는 격자가 어디서 감기는지 —
/// 여기에 적어 둔다.
/// </summary>
bool RunSpriteSheetTests()
{
    using GameEngine::Assets::Sprite;

    bool passed = true;
    float u = 0.0f;
    float v = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    // 타일셋: 512px를 32px 타일 16×16으로 나눈다. 마지막 타일은 오른쪽 아래 칸이고, 그 오른쪽
    // 끝은 이미지의 끝과 정확히 같아야 한다 — 넘으면 다음 줄 픽셀을 물어 온다.
    const Sprite tileset(1, "Tileset.png", "Tileset.png", 0, 0, 32.0f, {}, { 16, 16, 0, 12.0f });
    tileset.GetFrameRect(255, u, v, width, height);
    const bool lastTileIsBottomRight = u + width <= 1.0f && v + height <= 1.0f &&
        u > 0.9f && v > 0.9f;
    passed &= Expect(
        tileset.GetSheet().GetFrameCount() == 256,
        "a 16x16 tileset should hold 256 tiles");
    passed &= Expect(
        lastTileIsBottomRight, "the last tile should end at the edge of the image, not past it");

    // 상자 시트: 8×8 격자인데 위 네 줄만 쓴다. frameCount가 뒤의 빈 줄을 잘라내므로, 32번은
    // 없는 프레임이 아니라 처음으로 감긴다 — 애니메이터가 구간 끝을 넘겨도 빈 칸이 나오지 않는다.
    const Sprite chest(2, "Chest.png", "Chest.png", 0, 0, 32.0f, {}, { 8, 8, 32, 10.0f });
    chest.GetFrameRect(31, u, v, width, height);
    const bool lastUsedFrameIsRow3Col7 = u == 7.0f / 8.0f && v == 3.0f / 8.0f;
    chest.GetFrameRect(32, u, v, width, height);
    const bool wrapsAtFrameCount = u == 0.0f && v == 0.0f;
    passed &= Expect(
        chest.GetSheet().GetFrameCount() == 32,
        "a frameCount should trim the rows a chest sheet does not use");
    passed &= Expect(lastUsedFrameIsRow3Col7, "frame 31 should be the last cell of the fourth row");
    passed &= Expect(wrapsAtFrameCount, "the frame after the last used one should wrap to the first");

    // 한 클립은 한 줄이다: 상자 하나가 여는 7장이고, 다음 상자는 그 줄의 시작에서 8을 더한 자리다.
    chest.GetFrameRect(8, u, v, width, height);
    const bool secondRowStartsAtEight = u == 0.0f && v == 1.0f / 8.0f;
    passed &= Expect(secondRowStartsAtEight, "each chest clip should start at a multiple of the row");

    // 불꽃 시트: 200px과 128px를 여섯으로 나눈다. 셀은 33.3px과 21.3px이라 정수가 아니지만,
    // 프레임 사각형은 정규화 좌표라 나눗셈이 픽셀에서 일어나지 않는다. 확인할 것은 여섯 칸이
    // 이미지를 정확히 덮는가 — 마지막 칸의 오른쪽 끝이 1을 넘지 않는가 — 이다. float에서
    // 5 * (1/6) + (1/6)은 정확히 1.0으로 떨어진다.
    const Sprite flame(3, "Flame.png", "Flame.png", 0, 0, 32.0f, {}, { 6, 6, 35, 12.0f });
    flame.GetFrameRect(5, u, v, width, height);
    const bool lastColumnFits = u + width == 1.0f;
    flame.GetFrameRect(0, u, v, width, height);
    const bool firstStartsAtOrigin = u == 0.0f && v == 0.0f;
    passed &= Expect(
        lastColumnFits, "six columns should cover a non-integer cell size exactly, not past it");
    passed &= Expect(firstStartsAtOrigin, "the first frame should start at the image origin");

    // 시트가 채우지 못한 마지막 칸: 불꽃은 36칸 중 35장만 쓴다. 35번은 없으므로 처음으로 감긴다.
    flame.GetFrameRect(35, u, v, width, height);
    const bool unusedLastCellWraps = u == 0.0f && v == 0.0f;
    passed &= Expect(
        unusedLastCellWraps, "a sheet that leaves its last cell empty should wrap before it");

    return passed;
}
/// </summary>
bool RunResourceIdTests()
{
    // The case that would collide: every kind's own counter starts at one.
    const std::uint64_t firstMesh = GameEngine::Core::MakeResourceId(GameEngine::Core::ResourceIdDomain::Mesh, 1);
    const std::uint64_t firstTexture = GameEngine::Core::MakeResourceId(GameEngine::Core::ResourceIdDomain::Texture, 1);
    const std::uint64_t firstText = GameEngine::Core::MakeResourceId(GameEngine::Core::ResourceIdDomain::Text, 1);

    const bool distinct =
        firstMesh != firstTexture && firstTexture != firstText && firstMesh != firstText;
    const bool nonZero = firstMesh != 0 && firstTexture != 0 && firstText != 0;
    const bool domainsSurvive =
        GameEngine::Core::GetResourceIdDomain(firstMesh) == GameEngine::Core::ResourceIdDomain::Mesh &&
        GameEngine::Core::GetResourceIdDomain(firstTexture) == GameEngine::Core::ResourceIdDomain::Texture &&
        GameEngine::Core::GetResourceIdDomain(firstText) == GameEngine::Core::ResourceIdDomain::Text;

    // Within a kind, the counter still separates entries, and a large index keeps its kind.
    const std::uint64_t manyTextures = GameEngine::Core::MakeResourceId(GameEngine::Core::ResourceIdDomain::Texture, 1'000'000);
    const bool countsWithinKind =
        manyTextures != firstTexture &&
        GameEngine::Core::GetResourceIdDomain(manyTextures) == GameEngine::Core::ResourceIdDomain::Texture;

    return Expect(distinct, "the first id of each kind should differ") &&
        Expect(nonZero, "no id should be zero, which every IsValid treats as absent") &&
        Expect(domainsSurvive, "an id should report the kind it was made for") &&
        Expect(countsWithinKind, "ids within one kind should stay distinct and keep their kind");
}

bool RunLegacySidecarTests()
{
    TemporaryDirectory temporaryDirectory("legacy-sidecar");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    // 옛 이름만 있는 에셋 하나와, 두 이름이 다 있는 에셋 하나.
    if (!WriteFile(root / "Legacy.gameproject", "{}") ||
        !WriteFile(root / "Old.png", "png-data") ||
        !WriteFile(root / "Old.png.sprite.json", R"({"pixelsPerUnit": 64.0})") ||
        !WriteFile(root / "Both.png", "png-data") ||
        !WriteFile(root / "Both.png.sprite.json", R"({"pixelsPerUnit": 8.0})") ||
        !WriteFile(root / "Both.png.meta", R"({"pixelsPerUnit": 16.0})"))
    {
        return Expect(false, "could not create the legacy sidecar test data");
    }

    std::vector<std::string> captured;
    const GameEngine::Diagnostics::Debug::LogListenerId listener =
        GameEngine::Diagnostics::Debug::AddLogListener(
            [&captured](const GameEngine::Diagnostics::LogEntry& entry)
            {
                captured.push_back(entry.message);
            });

    const GameEngine::Platform::DirectoryContentSource content(root);
    GameEngine::Assets::AssetDatabase database;
    const bool refreshed = database.Refresh(content);
    GameEngine::Diagnostics::Debug::RemoveLogListener(listener);

    const auto* legacyOnly = database.FindAsset<GameEngine::Assets::Sprite>("Old.png");
    const auto* both = database.FindAsset<GameEngine::Assets::Sprite>("Both.png");

    // 옛 이름만 있으면 그것이 읽힌다. 개명 전의 프로젝트가 그대로 열리는 근거다.
    const bool readsLegacyName =
        legacyOnly != nullptr && legacyOnly->GetPixelsPerUnit() == 64.0f;
    // 두 이름이 다 있으면 지금 이름이 이긴다.
    const bool prefersCurrentName = both != nullptr && both->GetPixelsPerUnit() == 16.0f;

    // 로그가 어느 파일을 무엇으로 바꾸라는 것인지 정확히 말한다. "meta로 바꿔라"만으로는 사람이
    // 어느 이름을 지어야 하는지 모른다.
    bool namesTheRename = false;
    for (const std::string& message : captured)
    {
        if (message.find("Old.png.sprite.json") != std::string::npos &&
            message.find("Old.png.meta") != std::string::npos)
        {
            namesTheRename = true;
        }
    }
    // 지금 이름을 쓰는 에셋은 아무 말도 하지 않는다. 멀쩡한 것에 대한 경고는 곧 무시된다.
    bool silentAboutCurrentName = true;
    for (const std::string& message : captured)
    {
        if (message.find("Both.png") != std::string::npos &&
            message.find("Legacy sidecar") != std::string::npos)
        {
            silentAboutCurrentName = false;
        }
    }

    // 두 이름 모두 그 에셋의 것이므로 콘텐츠 브라우저는 둘 다 감춘다.
    const bool bothNamesAreOwned = database.FindSidecarOwner("Old.png.sprite.json") == legacyOnly &&
        database.FindSidecarOwner("Old.png.meta") == legacyOnly &&
        database.FindSidecarOwner("Both.png.sprite.json") == both &&
        database.FindSidecarOwner("Both.png.meta") == both;

    return Expect(refreshed, "a project holding a legacy sidecar should still open") &&
        Expect(readsLegacyName, "a sidecar under the old name should still be read") &&
        Expect(prefersCurrentName, "the current name should win when both exist") &&
        Expect(namesTheRename, "the warning should name both the old file and the new one") &&
        Expect(silentAboutCurrentName, "an asset already using the new name should not warn") &&
        Expect(bothNamesAreOwned, "both sidecar names should resolve to their asset");
}

bool RunAssetGuidTests()
{
    using GameEngine::Assets::AssetReference;
    using GameEngine::Core::Guid;

    // 값 타입부터: 32자리 16진수로 오가고, 경로처럼 생긴 것은 guid가 아니다.
    const std::optional<Guid> parsed = Guid::Parse("0123456789abcdef0123456789abcdef");
    const bool textRoundTrips = parsed && parsed->IsValid() &&
        parsed->ToString() == "0123456789abcdef0123456789abcdef";
    const bool rejectsNonGuid = !Guid::Parse("Textures/Albedo.png") && !Guid::Parse("") &&
        !Guid::Parse("0123456789abcdef0123456789abcde") &&
        !Guid::Parse("0123456789abcdef0123456789abcdeg") &&
        !Guid::Parse("00000000000000000000000000000000");
    // 접기는 어느 절반도 버리지 않는다. 상위만 다른 둘이 같은 키를 받으면 안 된다.
    const bool foldUsesBothHalves =
        Guid{ 1, 0 }.Fold() != Guid{ 0, 0 }.Fold() && Guid{ 0, 1 }.Fold() != Guid{ 0, 0 }.Fold() &&
        Guid{ 1, 0 }.Fold() != Guid{ 0, 1 }.Fold();
    // 발급된 것은 서로 다르고 유효하다.
    const Guid first = GameEngine::Core::MakeGuid();
    const Guid second = GameEngine::Core::MakeGuid();
    const bool issuesDistinct = first.IsValid() && second.IsValid() && !(first == second);

    // 참조 문자열은 두 형식을 다 읽는다. 저장은 가리키는 방식 그대로 적는다.
    const AssetReference byPath = AssetReference::Parse("Textures/Albedo.png#2");
    const AssetReference byGuid =
        AssetReference::Parse("0123456789abcdef0123456789abcdef#2");
    const bool readsBothForms = !byPath.IsGuidReference() &&
        byPath.GetPath().generic_string() == "Textures/Albedo.png" && byPath.GetLocalId() == 2 &&
        byGuid.IsGuidReference() && byGuid.GetGuid() == *parsed && byGuid.GetLocalId() == 2 &&
        byGuid.ToString() == "0123456789abcdef0123456789abcdef#2" &&
        byPath.ToString() == "Textures/Albedo.png#2";

    // 이제 진짜 프로젝트로. 하나는 guid를 가진 사이드카를, 하나는 사이드카가 없다.
    TemporaryDirectory temporaryDirectory("asset-guid");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::string identified = "11112222333344445555666677778888";
    if (!WriteFile(root / "Guid.gameproject", "{}") ||
        !WriteFile(root / "Known.png", "png-data") ||
        !WriteFile(root / "Known.png.meta", R"({"guid": ")" + identified + R"("})") ||
        !WriteFile(root / "Nameless.png", "png-data"))
    {
        return Expect(false, "could not create the asset guid test data");
    }

    const GameEngine::Platform::DirectoryContentSource content(root);
    GameEngine::Assets::AssetDatabase database;
    const bool refreshed = database.Refresh(content);
    const auto* known = database.FindAsset("Known.png");
    const auto* nameless = database.FindAsset("Nameless.png");

    // 사이드카의 guid가 정체성이 되고, 그것으로 찾을 수 있다.
    const bool readsGuidFromSidecar = known && known->GetGuid() == *Guid::Parse(identified) &&
        database.FindAsset(*Guid::Parse(identified)) == known;
    // guid 없는 에셋은 정체성이 없고, 정체성으로 닿을 수 없다.
    const bool namelessHasNoGuid = nameless && !nameless->GetGuid().IsValid();
    // 조회 키는 guid에서 접어 만든다.
    const bool keyComesFromGuid = known && known->GetId() == Guid::Parse(identified)->Fold();

    // GUID가 있는 에셋은 이동해도 조회 키가 같아야 한다.
    std::error_code moveError;
    std::filesystem::create_directories(root / "Moved", moveError);
    std::filesystem::rename(root / "Known.png", root / "Moved" / "Known.png", moveError);
    std::filesystem::rename(root / "Known.png.meta", root / "Moved" / "Known.png.meta", moveError);
    GameEngine::Assets::AssetDatabase afterMove;
    const bool rescanned = !moveError && afterMove.Refresh(content);
    const auto* moved = rescanned ? afterMove.FindAsset("Moved/Known.png") : nullptr;
    const bool keySurvivesMove = moved && moved->GetGuid() == *Guid::Parse(identified) &&
        moved->GetId() == (known ? known->GetId() : 0) &&
        afterMove.FindAsset(*Guid::Parse(identified)) == moved;

    // GUID가 없는 에셋의 조회 키는 경로에서 나오므로 이동하면 달라진다.
    // GPU 리소스 ID에는 콘텐츠 해시도 포함되므로 같은 파일의 픽셀과 다른 파일의 픽셀을 구분한다.
    const GameEngine::Assets::ImportIdentity before{ 1, 0xABCDu };
    const GameEngine::Assets::ImportIdentity after{ 2, 0xABCDu };
    const bool resourceIdFollowsTheKey =
        before.MakeResourceId(GameEngine::Core::ResourceIdDomain::Texture, 0) !=
        after.MakeResourceId(GameEngine::Core::ResourceIdDomain::Texture, 0);
    const GameEngine::Assets::ImportIdentity sameFile{ 1, 0xABCDu };
    const bool resourceIdStableForSameInput =
        before.MakeResourceId(GameEngine::Core::ResourceIdDomain::Texture, 0) ==
        sameFile.MakeResourceId(GameEngine::Core::ResourceIdDomain::Texture, 0);

    return Expect(textRoundTrips, "a guid should survive a trip through its text form") &&
        Expect(rejectsNonGuid, "text that is not 32 hex digits should not parse as a guid") &&
        Expect(foldUsesBothHalves, "folding to a key should use both halves") &&
        Expect(issuesDistinct, "issued guids should be valid and distinct") &&
        Expect(readsBothForms, "a reference should read a path form and a guid form") &&
        Expect(refreshed, "the guid test project should open") &&
        Expect(readsGuidFromSidecar, "a sidecar guid should become the asset's identity") &&
        Expect(namelessHasNoGuid, "an asset without a sidecar guid should have no identity") &&
        Expect(keyComesFromGuid, "the lookup key should be folded from the guid") &&
        Expect(keySurvivesMove, "moving an identified asset should not change its key") &&
        Expect(
            resourceIdFollowsTheKey && resourceIdStableForSameInput,
            "a resource id should follow the key and stay put for the same file");
}

bool RunAssetMoveMatchingTests()
{
    using GameEngine::Assets::ArrivedFile;
    using GameEngine::Assets::AssetMove;
    using GameEngine::Assets::DepartedAsset;
    using GameEngine::Assets::MatchAssetMoves;
    using GameEngine::Assets::MoveRefusal;
    using GameEngine::Core::Guid;

    const Guid identity{ 0x1111u, 0x2222u };
    const auto judge = [&identity](
        const std::filesystem::path& from, const std::filesystem::path& to,
        const bool destinationHasSidecar, const std::uint64_t toHash = 7)
    {
        const DepartedAsset departed[]{ { from, 7, identity } };
        const ArrivedFile arrived[]{ { to, toHash, destinationHasSidecar } };
        const std::vector<AssetMove> moves = MatchAssetMoves(departed, arrived);
        // 내용이 다르면 짝이 아예 없다. 그 경우를 「거절」과 섞지 않기 위해 따로 답한다.
        return moves.empty() ? std::optional<MoveRefusal>{} : std::optional{ moves.front().refusal };
    };

    const bool acceptsAMove =
        judge("Textures/Albedo.png", "Textures/Deep/Albedo.png", false) == MoveRefusal::None;
    const bool acceptsARename =
        judge("Textures/Albedo.png", "Textures/Base.png", false) == MoveRefusal::None;
    // 확장자가 바뀐 것은 이동이 아니다. 옮기기가 형식을 바꾸지는 않는다.
    const bool refusesADifferentExtension =
        judge("Textures/Albedo.png", "Textures/Albedo.tga", false) ==
        MoveRefusal::DifferentExtension;
    // 이름도 폴더도 둘 다 다르면 「지우고 다른 것을 넣었다」와 구별할 수 없다.
    const bool refusesAnUnrelatedLocation =
        judge("Textures/Albedo.png", "Models/Base.png", false) == MoveRefusal::UnrelatedLocation;
    const bool refusesAnOccupiedDestination =
        judge("Textures/Albedo.png", "Textures/Deep/Albedo.png", true) ==
        MoveRefusal::DestinationHasSidecar;
    // 내용이 다르면 후보가 아니다 — 사라진 것은 지워진 것이고, 그 판단은 여기 몫이 아니다.
    const bool ignoresDifferentContent =
        !judge("Textures/Albedo.png", "Textures/Deep/Albedo.png", false, 9);

    // 같은 내용이 여럿이면 어느 쪽으로도 찍지 않는다. 양쪽 모두 유일할 때만 이을 수 있다.
    const DepartedAsset twoLeft[]{
        { "A.png", 7, identity }, { "B.png", 7, Guid{ 0x3333u, 0x4444u } } };
    const ArrivedFile oneArrived[]{ { "Sub/A.png", 7, false } };
    const std::vector<AssetMove> fromTwo = MatchAssetMoves(twoLeft, oneArrived);
    const bool refusesTwoDeparted = fromTwo.size() == 2 &&
        fromTwo[0].refusal == MoveRefusal::SeveralCandidates &&
        fromTwo[1].refusal == MoveRefusal::SeveralCandidates;

    const DepartedAsset oneLeft[]{ { "A.png", 7, identity } };
    const ArrivedFile twoArrived[]{ { "Sub/A.png", 7, false }, { "Sub/Copy.png", 7, false } };
    const std::vector<AssetMove> toTwo = MatchAssetMoves(oneLeft, twoArrived);
    const bool refusesTwoArrived =
        toTwo.size() == 1 && toTwo.front().refusal == MoveRefusal::SeveralCandidates;

    // 정체성이 없는 에셋은 옮길 것이 없다. 새 자리에서 새로 발급받는 것이 맞다.
    const DepartedAsset nameless[]{ { "A.png", 7, Guid{} } };
    const bool ignoresAnAssetWithNoIdentity = MatchAssetMoves(nameless, oneArrived).empty();

    // 이은 짝은 정체성과 두 경로를 그대로 실어 나른다. 파일을 옮기는 쪽이 그것만 보고 움직인다.
    const ArrivedFile destination[]{ { "Sub/A.png", 7, false } };
    const std::vector<AssetMove> carried = MatchAssetMoves(oneLeft, destination);
    const bool carriesTheIdentity = carried.size() == 1 && carried.front().guid == identity &&
        carried.front().from == std::filesystem::path("A.png") &&
        carried.front().to == std::filesystem::path("Sub/A.png");

    return Expect(acceptsAMove, "a file that kept its name in a new folder should be a move") &&
        Expect(acceptsARename, "a file that kept its folder under a new name should be a move") &&
        Expect(refusesADifferentExtension, "a different extension should not be a move") &&
        Expect(
            refusesAnUnrelatedLocation,
            "a file whose name and folder both changed should not be a move") &&
        Expect(
            refusesAnOccupiedDestination,
            "a destination that already carries metadata should not be a move") &&
        Expect(ignoresDifferentContent, "different contents should not be paired at all") &&
        Expect(refusesTwoDeparted, "two assets sharing contents should both be refused") &&
        Expect(refusesTwoArrived, "two candidates sharing contents should be refused") &&
        Expect(ignoresAnAssetWithNoIdentity, "an asset with no identity has nothing to move") &&
        Expect(carriesTheIdentity, "a matched move should carry the identity and both paths");
}

static const TestSupport::Registration gAssetDatabaseTests{
    "AssetDatabase", "asset database tests should pass", RunAssetDatabaseTests };

static const TestSupport::Registration gAssetImporterTests{
    "AssetDatabase", "asset importer registry tests should pass", RunAssetImporterTests };

static const TestSupport::Registration gLegacySidecarTests{
    "AssetDatabase", "legacy sidecar tests should pass", RunLegacySidecarTests };

static const TestSupport::Registration gAssetGuidTests{
    "AssetDatabase", "asset guid tests should pass", RunAssetGuidTests };

static const TestSupport::Registration gAssetMoveMatchingTests{
    "AssetDatabase", "asset move matching tests should pass", RunAssetMoveMatchingTests };

static const TestSupport::Registration gAssetResidencyTests{
    "AssetDatabase", "asset residency tests should pass", RunAssetResidencyTests };

static const TestSupport::Registration gModelInstantiationTests{
    "AssetDatabase", "model instantiation tests should pass", RunModelInstantiationTests };

static const TestSupport::Registration gAssetReferenceTests{
    "AssetDatabase", "asset reference tests should pass", RunAssetReferenceTests };

static const TestSupport::Registration gResourceIdTests{
    "AssetDatabase", "resource id tests should pass", RunResourceIdTests };

static const TestSupport::Registration gSpriteSheetTests{
    "AssetDatabase", "sprite sheet tests should pass", RunSpriteSheetTests };
