#include "AssetDatabaseTests.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>

#include "Assets/AssetDatabase.h"
#include "Assets/AssetImporterRegistry.h"
#include "Assets/AssetReference.h"
#include "Assets/MeshData.h"
#include "Core/ResourceId.h"
#include "Platform/DirectoryContentSource.h"

#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using TestSupport::TemporaryDirectory;
    using TestSupport::WriteFile;

    /// <summary>
    /// 바이트를 파싱하는 시늉만 하는 임포터다. 진짜 이미지 디코더는 플랫폼을 부르므로, 상주
    /// 페이로드가 있는지 없는지만 묻는 이 시험에는 이것이면 된다.
    /// </summary>
    class InheritTestImporter final : public GameEngine::Assets::IAssetImporter
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
            if (mode == GameEngine::Assets::ImportMode::Structure)
            {
                return true;
            }
            auto mesh = std::make_shared<GameEngine::Assets::MeshData>();
            mesh->id = identity.MakeResourceId(GameEngine::Core::ResourceIdDomain::Mesh, 0);
            mesh->vertices.resize(3);
            mesh->indices = { 0, 1, 2 };
            contents.meshes.push_back(std::move(mesh));
            return true;
        }
    };

    /// <summary>이 경로의 메시 페이로드를 읽어 그 주소를 답한다. 없으면 null이다.</summary>
    [[nodiscard]] const void* LoadMeshAddress(
        const GameEngine::Assets::AssetDatabase& database, const std::string& path)
    {
        const std::shared_ptr<const GameEngine::Assets::MeshData> mesh =
            database.LoadMesh(GameEngine::Assets::AssetReference::Parse(path));
        return mesh.get();
    }
}

bool RunAssetRescanInheritTests()
{
    const TestSupport::RegistryScope registries;
    using GameEngine::Assets::AssetDatabase;
    using GameEngine::Assets::AssetImporterRegistry;
    using GameEngine::Platform::DirectoryContentSource;

    static const InheritTestImporter importer;
    const bool registered = AssetImporterRegistry::Register(".inheritmesh", importer);

    // 사이드카를 손으로 적는다. 정체성은 사이드카에서만 오고, 그것이 물려받기의 전제다 —
    // guid가 없는 에셋은 짝지을 수 없어 그냥 다시 읽힌다.
    const auto identity = [](const char* const guid)
    {
        return std::string(R"({"guid":")") + guid + R"("})";
    };
    const TemporaryDirectory temporaryDirectory("rescan-inherit");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const bool wrote = WriteFile(root / "Inherit.gameproject", "{}") &&
        WriteFile(root / "Inherit.gameproject.meta",
            identity("00000000000000000000000000000010")) &&
        WriteFile(root / "Models" / "Steady.inheritmesh", "steady-bytes") &&
        WriteFile(root / "Models" / "Steady.inheritmesh.meta",
            identity("00000000000000000000000000000011")) &&
        WriteFile(root / "Models" / "Edited.inheritmesh", "edited-bytes") &&
        WriteFile(root / "Models" / "Edited.inheritmesh.meta",
            identity("00000000000000000000000000000012")) &&
        WriteFile(root / "Models" / "Moved.inheritmesh", "moved-bytes") &&
        WriteFile(root / "Models" / "Moved.inheritmesh.meta",
            identity("00000000000000000000000000000013"));
    if (!wrote)
    {
        return Expect(false, "the fixture project could not be written");
    }

    const DirectoryContentSource source(root);
    AssetDatabase before;
    const bool beforeScanned = before.Refresh(source);

    const void* steadyBefore = beforeScanned ? LoadMeshAddress(before, "Models/Steady.inheritmesh") : nullptr;
    const void* editedBefore = beforeScanned ? LoadMeshAddress(before, "Models/Edited.inheritmesh") : nullptr;
    const void* movedBefore = beforeScanned ? LoadMeshAddress(before, "Models/Moved.inheritmesh") : nullptr;
    const bool everythingResident =
        steadyBefore != nullptr && editedBefore != nullptr && movedBefore != nullptr;

    // 한 파일은 그대로, 한 파일은 내용이 바뀌고, 한 파일은 자리를 옮긴다. 사이드카도 함께
    // 옮겨야 옮겨진 파일이 같은 guid를 들고 나타난다.
    const bool changed =
        WriteFile(root / "Models" / "Edited.inheritmesh", "edited-bytes-v2");
    std::error_code error;
    std::filesystem::create_directories(root / "Models" / "Nested", error);
    std::filesystem::rename(
        root / "Models" / "Moved.inheritmesh", root / "Models" / "Nested" / "Moved.inheritmesh",
        error);
    const bool metaMoved = !error &&
        (std::filesystem::exists(root / "Models" / "Moved.inheritmesh.meta")
                ? (std::filesystem::rename(
                       root / "Models" / "Moved.inheritmesh.meta",
                       root / "Models" / "Nested" / "Moved.inheritmesh.meta", error),
                    !error)
                : true);

    AssetDatabase after;
    const bool afterScanned = after.Refresh(source);
    if (afterScanned)
    {
        after.InheritPayloadsFrom(before);
    }

    // 물려받은 것은 같은 객체다: 다시 읽었다면 주소가 다르다.
    const void* steadyAfter = afterScanned ? LoadMeshAddress(after, "Models/Steady.inheritmesh") : nullptr;
    const void* editedAfter = afterScanned ? LoadMeshAddress(after, "Models/Edited.inheritmesh") : nullptr;
    const void* movedAfter =
        afterScanned ? LoadMeshAddress(after, "Models/Nested/Moved.inheritmesh") : nullptr;

    const bool unchangedIsInherited = steadyAfter != nullptr && steadyAfter == steadyBefore;
    const bool editedIsReloaded = editedAfter != nullptr && editedAfter != editedBefore;
    const bool movedIsInherited = movedAfter != nullptr && movedAfter == movedBefore;

    // 실패한 스캔은 아무것도 물려주지 않고, 무엇보다 옛 데이터베이스를 건드리지 않는다.
    AssetDatabase failed;
    const bool failedScanRefused = !failed.Refresh(DirectoryContentSource(root / "NoSuchPlace"));
    failed.InheritPayloadsFrom(before);
    const bool failedInheritsNothing = failed.GetLoadedPayloadCount() == 0;
    const bool previousUntouched =
        LoadMeshAddress(before, "Models/Steady.inheritmesh") == steadyBefore;


    return Expect(registered, "the fixture importer should register") &&
        Expect(beforeScanned && everythingResident, "every fixture asset should load once") &&
        Expect(changed && metaMoved, "the fixture files should change and move") &&
        Expect(afterScanned, "the rescan should succeed") &&
        Expect(
            unchangedIsInherited,
            "an asset whose bytes did not change should keep the payload already read") &&
        Expect(
            editedIsReloaded,
            "an edited asset must be read again, or the editor would show what was saved before") &&
        Expect(
            movedIsInherited,
            "a moved asset keeps its identity, so it should keep its payload as well") &&
        Expect(
            failedScanRefused && failedInheritsNothing,
            "a scan that failed has nothing to inherit into") &&
        Expect(previousUntouched, "inheriting must not disturb the database it took from");
}

static const TestSupport::Registration gAssetRescanInheritTests{
    "AssetDatabase", "asset rescan inherit tests should pass", RunAssetRescanInheritTests };
