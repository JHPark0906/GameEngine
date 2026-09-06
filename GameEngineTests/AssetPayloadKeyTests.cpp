#include "AssetPayloadKeyTests.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

#include "Assets/AssetDatabase.h"
#include "Assets/AssetImporter.h"
#include "Assets/AssetImporterRegistry.h"
#include "Platform/DirectoryContentSource.h"
#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::RegistryScope;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{
    /// <summary>임포트한 횟수를 센다. 「다시 임포트했는가」는 그 수로만 답할 수 있다.</summary>
    class CountingMeshImporter final : public GameEngine::Assets::IAssetImporter
    {
    public:
        mutable int imports = 0;

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
            ++imports;
            auto mesh = std::make_shared<GameEngine::Assets::MeshData>();
            mesh->id = identity.MakeResourceId(GameEngine::Core::ResourceIdDomain::Mesh, 0);
            mesh->vertices.resize(3);
            mesh->indices = { 0, 1, 2 };
            contents.meshes.push_back(std::move(mesh));
            return true;
        }
    };

    constexpr const char* ModelGuid = "5f60718293a4b5c6d7e8f90a1b2c3d4e";

    [[nodiscard]] bool WriteProject(
        const std::filesystem::path& root, const std::filesystem::path& modelPath)
    {
        const auto sidecar = [](const char* const guid)
        {
            return std::string(R"({"format":"gameengine-meta/1","guid":")") + guid + R"("})";
        };
        return WriteFile(root / "Keyed.gameproject", "{}") &&
            WriteFile(root / "Keyed.gameproject.meta",
                sidecar("60718293a4b5c6d7e8f90a1b2c3d4e5f")) &&
            WriteFile(root / modelPath, "model-bytes") &&
            WriteFile(root / (modelPath.string() + ".meta"), sidecar(ModelGuid));
    }
}

bool RunAssetPayloadKeyTests()
{
    namespace Assets = GameEngine::Assets;

    const RegistryScope registries;
    static const CountingMeshImporter importer;
    if (!Expect(
            Assets::AssetImporterRegistry::Register(".keyedmodel", importer),
            "the counting importer should register"))
    {
        return false;
    }

    TemporaryDirectory temporaryDirectory("asset-payload-key");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::filesystem::path first = "Models/Only.keyedmodel";
    if (!Expect(WriteProject(root, first), "the keyed project should be written"))
    {
        return false;
    }

    const GameEngine::Platform::DirectoryContentSource content(root);
    Assets::AssetDatabase table;
    if (!Expect(table.Refresh(content), "the keyed project should scan"))
    {
        return false;
    }

    // 페이로드를 한 번 로드해 둔다. 이 뒤의 모든 질문은 「그것이 아직 거기 있는가」다.
    const std::shared_ptr<const Assets::MeshData> loaded =
        table.LoadMesh(Assets::AssetReference(first.generic_string(), 0));
    const bool loadedOnce = loaded != nullptr && importer.imports == 1 &&
        table.GetLoadedPayloadCount() == 1;
    // 🔴 GPU 리소스 id의 씨앗이다. 이 단위는 키를 바꿀 뿐 이 값을 건드리지 않아야 한다 —
    // 움직이면 그것은 구조 변경이 아니라 동작 변경이다.
    const std::uint64_t resourceIdBefore = loaded ? loaded->id : 0;

    // 🔴 표를 다시 세우면 캐시에 옛 항목이 남지 않는다. 남으면 그것은 이미 죽은 에셋을 가리키는
    // 키다.
    const bool rescanned = table.Refresh(content);
    const bool rebuildEmptiesTheCache = rescanned && table.GetLoadedPayloadCount() == 0;

    // 🔴 파일을 옮긴다. guid가 있으므로 새 표가 옛 표에게서 페이로드를 물려받아야 하고,
    // 임포터는 다시 불리지 않아야 한다.
    const std::filesystem::path moved = "Models/Moved/Only.keyedmodel";
    const std::shared_ptr<const Assets::MeshData> beforeMove =
        table.LoadMesh(Assets::AssetReference(first.generic_string(), 0));
    const int importsBeforeMove = importer.imports;
    std::error_code error;
    std::filesystem::create_directories(root / "Models" / "Moved", error);
    std::filesystem::rename(root / first, root / moved, error);
    std::filesystem::rename(
        root / (first.string() + ".meta"), root / (moved.string() + ".meta"), error);

    Assets::AssetDatabase afterMove;
    const bool movedScan = !error && afterMove.Refresh(content);
    afterMove.InheritPayloadsFrom(table);
    const std::shared_ptr<const Assets::MeshData> afterMoveMesh =
        afterMove.LoadMesh(Assets::AssetReference(moved.generic_string(), 0));
    const bool movedKeepsItsPayload = movedScan && afterMoveMesh != nullptr &&
        importer.imports == importsBeforeMove;
    const bool seedSurvivedTheMove = afterMoveMesh && afterMoveMesh->id == resourceIdBefore;

    // 내용이 바뀐 파일은 반대다: 옛 페이로드를 잊고 다시 임포트해야 한다.
    const bool rewrote = WriteFile(root / moved, "different-model-bytes");
    Assets::AssetDatabase afterEdit;
    const int importsBeforeEdit = importer.imports;
    const bool editScan = rewrote && afterEdit.Refresh(content);
    afterEdit.InheritPayloadsFrom(afterMove);
    const std::shared_ptr<const Assets::MeshData> afterEditMesh =
        afterEdit.LoadMesh(Assets::AssetReference(moved.generic_string(), 0));
    const bool changedFileIsReimported = editScan && afterEditMesh != nullptr &&
        importer.imports == importsBeforeEdit + 1;

    return Expect(loadedOnce, "a first load should import once and hold one file") &&
        Expect(
            rebuildEmptiesTheCache,
            "rebuilding the table should leave the cache holding nothing") &&
        Expect(
            movedKeepsItsPayload,
            "an asset with an identity should keep its payload when its file moves") &&
        Expect(
            seedSurvivedTheMove,
            "the resource id a payload was built with should not move with the key") &&
        Expect(
            changedFileIsReimported,
            "a file whose contents changed should be imported again");
}

static const TestSupport::Registration gAssetPayloadKeyTests{
    "AssetDatabase", "asset payload key tests should pass", RunAssetPayloadKeyTests };
