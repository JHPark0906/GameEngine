#include "AssetDatabasePartsTests.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "Assets/AssetDatabase.h"
#include "Assets/AssetImporter.h"
#include "Assets/AssetImporterRegistry.h"
#include "Assets/AssetManifest.h"
#include "Assets/AssetPayloadCache.h"
#include "Platform/DirectoryContentSource.h"
#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{
    /// <summary>메시 하나를 담는 모델이다. 페이로드가 실제로 만들어지는 것이 이 시험에 필요하다.</summary>
    class OneMeshImporter final : public GameEngine::Assets::IAssetImporter
    {
    public:
        [[nodiscard]] GameEngine::Assets::AssetType GetAssetType() const override
        {
            return GameEngine::Assets::AssetType::Mesh;
        }

        [[nodiscard]] bool Import(
            const std::filesystem::path&,
            std::span<const std::byte>,
            const GameEngine::Assets::ImportIdentity& identity,
            const GameEngine::Assets::ImportMode mode,
            GameEngine::Assets::ImportedContents& contents,
            std::string&) const override
        {
            contents.subAssets.push_back({ GameEngine::Assets::AssetType::Mesh, "Only" });
            if (mode == GameEngine::Assets::ImportMode::Structure)
            {
                return true;
            }
            auto mesh = std::make_shared<GameEngine::Assets::MeshData>();
            mesh->id = identity.MakeResourceId(GameEngine::Assets::ResourceIdDomain::Mesh, 0);
            mesh->vertices.resize(3);
            mesh->indices = { 0, 1, 2 };
            contents.meshes.push_back(std::move(mesh));
            return true;
        }
    };

    [[nodiscard]] bool WriteProject(const std::filesystem::path& root)
    {
        const auto sidecar = [](const char* const guid)
        {
            return std::string(R"({"format":"gameengine-meta/1","guid":")") + guid + R"("})";
        };
        return WriteFile(root / "Parts.gameproject", "{}") &&
            WriteFile(root / "Parts.gameproject.meta",
                sidecar("3d4e5f60718293a4b5c6d7e8f90a1b2c")) &&
            WriteFile(root / "Models/Only.partsmodel", "model-bytes") &&
            WriteFile(root / "Models/Only.partsmodel.meta",
                sidecar("4e5f60718293a4b5c6d7e8f90a1b2c3d"));
    }
}

bool RunAssetDatabasePartsTests()
{
    const TestSupport::RegistryScope registries;
    namespace Assets = GameEngine::Assets;

    static const OneMeshImporter importer;
    const bool registered = Assets::AssetImporterRegistry::Register(".partsmodel", importer);

    TemporaryDirectory temporaryDirectory("asset-database-parts");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    bool passed = Expect(registered, "the parts model importer should register") &&
        Expect(WriteProject(root), "the parts project should be written");
    if (!passed)
    {
        return false;
    }

    const GameEngine::Platform::DirectoryContentSource content(root);
    Assets::AssetDatabase table;
    passed &= Expect(table.Refresh(content), "the parts project should scan");

    // 🔴 매니페스트는 표만 있으면 된다. 쓰고 다시 읽는 동안 페이로드는 하나도 로드되지 않는다 —
    // 그것이 빌드가 이 프로젝트를 열어 패키징하면서 파일을 한 개도 임포트하지 않는 이유다.
    const std::string text = Assets::WriteManifestText(table.GetAssets());
    const std::vector<std::byte> bytes{
        reinterpret_cast<const std::byte*>(text.data()),
        reinterpret_cast<const std::byte*>(text.data() + text.size()) };
    const std::optional<std::vector<Assets::ManifestRecord>> records =
        Assets::ReadManifestText(bytes);
    const bool manifestNeedsNoPayloads = !text.empty() && records && records->size() == 2 &&
        table.GetLoadedPayloadCount() == 0;

    // 🔴 페이로드 캐시는 매니페스트 없이 혼자 선다. 표에게 묻는 것은 어느 에셋인지 하나뿐이고,
    // 그 뒤로는 캐시와 콘텐츠 소스만 있으면 된다.
    const Assets::Asset* const model = table.FindAsset("Models/Only.partsmodel");
    Assets::AssetPayloadCache cache;
    const Assets::LoadedFile* const loaded = model ? cache.Load(*model, content) : nullptr;
    const bool cacheStandsAlone = loaded != nullptr && loaded->meshes.size() == 1 &&
        cache.GetLoadedCount() == 1;

    // 그리고 혼자 퇴거한다. 무엇이 참조되는지는 부르는 쪽이 말하고, 캐시는 그 답만 받는다.
    cache.UnloadUnreferenced({});
    const bool cacheEvictsAlone = cache.GetLoadedCount() == 0;

    // 표는 그동안 아무것도 로드하지 않았다: 페이로드를 쥔 것은 캐시뿐이다.
    const bool tableStayedEmpty = table.GetLoadedPayloadCount() == 0;


    return passed &&
        Expect(
            manifestNeedsNoPayloads,
            "writing and reading a manifest should load no payloads at all") &&
        Expect(
            cacheStandsAlone,
            "the payload cache should load a file with no manifest and no database") &&
        Expect(cacheEvictsAlone, "the payload cache should evict on its own") &&
        Expect(tableStayedEmpty, "payloads a part holds should not appear in the table");
}

static const TestSupport::Registration gAssetDatabasePartsTests{
    "AssetDatabase", "asset database parts tests should pass", RunAssetDatabasePartsTests };
