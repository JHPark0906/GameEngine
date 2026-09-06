#include "AssetDatabase.h"
#include "AssetPayloadCache.h"
#include "AssetDatabaseInternal.h"
#include "AssetManifest.h"

#include "AssetImporterRegistry.h"
#include "../Platform/DirectoryContentSource.h"
#include "../Core/Json.h"
#include "../Platform/RelativePath.h"
#include "../Platform/TextFile.h"
#include "../Diagnostics/Debug.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace GameEngine::Assets
{

namespace
{

    std::string MakePathKey(const std::filesystem::path& relativePath)
    {
        std::string key = Internal::PathToUtf8(relativePath.lexically_normal());
        std::ranges::transform(
            key,
            key.begin(),
            [](const unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        return key;
    }

    AssetKey HashPath(const std::string_view pathKey)
    {
        std::uint64_t hash = Internal::FnvOffsetBasis;
        for (const unsigned char character : pathKey)
        {
            hash ^= character;
            hash *= Internal::FnvPrime;
        }
        return hash == 0 ? 1 : hash;
    }

    std::string ToHex(const std::uint64_t value)
    {
        std::ostringstream stream;
        stream << std::hex << std::setfill('0') << std::setw(16) << value;
        return stream.str();
    }

    std::optional<std::uint64_t> ParseHex(const std::string& value)
    {
        std::uint64_t result = 0;
        const auto conversion = std::from_chars(
            value.data(), value.data() + value.size(), result, 16);
        if (conversion.ec != std::errc{} || conversion.ptr != value.data() + value.size())
        {
            return std::nullopt;
        }
        return result;
    }

    std::optional<std::uintmax_t> ParseFileSize(const std::string& value)
    {
        std::uintmax_t result = 0;
        const auto conversion = std::from_chars(
            value.data(), value.data() + value.size(), result, 10);
        if (conversion.ec != std::errc{} || conversion.ptr != value.data() + value.size())
        {
            return std::nullopt;
        }
        return result;
    }

    void WriteEscapedJsonString(std::ostream& stream, const std::string_view value)
    {
        stream << '"';
        constexpr char HexDigits[] = "0123456789abcdef";
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '"': stream << "\\\""; break;
            case '\\': stream << "\\\\"; break;
            case '\b': stream << "\\b"; break;
            case '\f': stream << "\\f"; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default:
                if (character < 0x20)
                {
                    stream << "\\u00" << HexDigits[character >> 4] << HexDigits[character & 0x0f];
                }
                else
                {
                    stream << static_cast<char>(character);
                }
                break;
            }
        }
        stream << '"';
    }


    /// <summary>
    /// 형식에 맞는 에셋 객체를 만든다. 여기서 아는 것은 형식뿐이다 — 그 형식만의 메타데이터는
    /// 만들어진 객체가 자기 훅으로 채운다.
    /// </summary>
    std::unique_ptr<Asset> CreateAsset(
        const AssetKey id,
        const AssetType type,
        const std::filesystem::path& relativePath,
        const std::filesystem::path& sourcePath,
        const std::uint64_t contentHash,
        const std::uintmax_t fileSize)
    {
        switch (type)
        {
        case AssetType::ProjectSettings:
            return std::make_unique<ProjectSettingsAsset>(
                id, relativePath, sourcePath, contentHash, fileSize);
        case AssetType::Scene:
            return std::make_unique<SceneAsset>(
                id, relativePath, sourcePath, contentHash, fileSize);
        case AssetType::Sprite:
            return std::make_unique<Sprite>(
                id, relativePath, sourcePath, contentHash, fileSize);
        case AssetType::Mesh:
            return std::make_unique<Mesh>(
                id, relativePath, sourcePath, contentHash, fileSize);
        case AssetType::Font:
            return std::make_unique<Font>(
                id, relativePath, sourcePath, contentHash, fileSize);
        case AssetType::AudioClip:
            return std::make_unique<AudioClip>(
                id, relativePath, sourcePath, contentHash, fileSize);
        case AssetType::Icon:
            return std::make_unique<Icon>(
                id, relativePath, sourcePath, contentHash, fileSize);
        case AssetType::Skeleton:
            return std::make_unique<Skeleton>(
                id, relativePath, sourcePath, contentHash, fileSize);
        case AssetType::AnimationClip:
            return std::make_unique<AnimationClip>(
                id, relativePath, sourcePath, contentHash, fileSize);
        case AssetType::SkinnedMesh:
            return std::make_unique<SkinnedMesh>(
                id, relativePath, sourcePath, contentHash, fileSize);
        case AssetType::Material:
            return std::make_unique<Material>(
                id, relativePath, sourcePath, contentHash, fileSize);
        }
        return nullptr;
    }

    /// <summary>
    /// 이 에셋의 사이드카 경로다. 지금 이름이 있으면 그것이고, 없고 옛 이름이 있으면 그것이다.
    /// 어느 쪽도 없으면 비어 있다.
    ///
    /// 있는지 없는지는 `presentFiles`가 답한다 — 스캔은 이미 디렉터리를 열거했으므로 파일 목록을
    /// 손에 쥐고 있다. 목록이 없는 단건 등록에서는 원본에 묻는다.
    /// </summary>
    [[nodiscard]] std::filesystem::path FindSidecarPath(
        const Platform::IContentSource& source,
        const Asset& asset,
        const std::unordered_set<std::string>* const presentFiles)
    {
        const auto exists = [&source, presentFiles](const std::filesystem::path& path)
        {
            return presentFiles ? presentFiles->contains(MakePathKey(path))
                                : source.Exists(path);
        };
        const std::string_view suffix = asset.GetSidecarSuffix();
        if (!suffix.empty())
        {
            std::filesystem::path path = asset.GetRelativePath();
            path += Internal::Utf8ToPath(std::string(suffix));
            if (exists(path))
            {
                return path;
            }
        }
        const std::string_view legacy = asset.GetLegacySidecarSuffix();
        if (!legacy.empty())
        {
            std::filesystem::path path = asset.GetRelativePath();
            path += Internal::Utf8ToPath(std::string(legacy));
            if (exists(path))
            {
                std::filesystem::path renamed = asset.GetRelativePath();
                renamed += Internal::Utf8ToPath(std::string(suffix));
                Diagnostics::Debug::LogWarning(
                    "Legacy sidecar found: ", Internal::PathToUtf8(path), " - rename to ",
                    Internal::PathToUtf8(renamed), ".");
                return path;
            }
        }
        return {};
    }

    /// <summary>
    /// 사이드카를 읽어 JSON으로 준다. 없거나 읽지 못하거나 형식이 깨졌으면 비어 있다.
    ///
    /// 파일을 한 번만 열고 한 번만 파싱하는 이유는 그 내용을 두 곳이 필요로 하기 때문이다:
    /// 정체성(guid)은 에셋이 만들어지기 전에 있어야 조회 키가 정해지고, 형식별 항목은 만들어진
    /// 에셋이 자기 훅으로 읽는다.
    /// </summary>
    [[nodiscard]] std::optional<Core::Json> ReadSidecar(
        const Platform::IContentSource& source,
        const Asset& asset,
        const std::unordered_set<std::string>* const presentFiles)
    {
        const std::filesystem::path sidecarPath = FindSidecarPath(source, asset, presentFiles);
        if (sidecarPath.empty())
        {
            return std::nullopt;
        }
        std::vector<std::byte> sidecarBytes;
        if (!source.Read(sidecarPath, sidecarBytes))
        {
            Diagnostics::Debug::LogError(
                "Failed to read asset metadata; the asset is registered with defaults. path=",
                sidecarPath.string());
            return std::nullopt;
        }
        try
        {
            return Core::Json::ParseBytes(sidecarBytes);
        }
        catch (const std::exception& exception)
        {
            // 사이드카 하나가 깨졌다고 프로젝트가 열리지 않아서는 안 된다. 기본값으로 등록하되
            // 조용히 넘어가지는 않는다 — 사람이 쓴 파일이고, 고칠 수 있는 것은 사람뿐이다.
            Diagnostics::Debug::LogError(
                "Failed to load asset metadata; the asset is registered with defaults. path=",
                sidecarPath.string(), ", error=", exception.what());
            return std::nullopt;
        }
    }

    /// <summary>사이드카가 적어 둔 정체성이다. 없거나 형식이 아니면 비어 있다.</summary>
    [[nodiscard]] Core::Guid ReadGuid(const Core::Json& sidecar)
    {
        const Core::Json* const value = sidecar.IsObject() ? sidecar.Find("guid") : nullptr;
        if (!value || !value->IsString())
        {
            return {};
        }
        const std::optional<Core::Guid> guid = Core::Guid::Parse(value->Get<std::string>());
        return guid ? *guid : Core::Guid{};
    }

}

bool AssetDatabase::Refresh(const Platform::IContentSource& source)
{
    Clear();
    mContentSource = &source;
    mProjectRootPath = source.GetDescription();

    // 어느 파일이 등록에 실패했는지를 붙잡아 둔다. 이것이 없으면 파일 하나가 프로젝트 열기를
    // 막아도 사용자가 보는 것은 그 파일을 지목하지 않는 문장뿐이다.
    // 열거는 한 번만 한다. 등록이 사이드카를 찾을 때 파일시스템에 다시 묻는 대신 이 목록에
    // 물어, 존재하지 않는 사이드카를 반복해서 열지 않는다.
    const std::vector<std::filesystem::path> files = source.List();
    std::unordered_set<std::string> presentFiles;
    presentFiles.reserve(files.size());
    for (const std::filesystem::path& relativePath : files)
    {
        presentFiles.insert(MakePathKey(relativePath));
    }

    std::filesystem::path firstFailedPath;
    for (const std::filesystem::path& relativePath : files)
    {
        if (!GetAssetType(relativePath))
        {
            continue;
        }
        if (!RegisterAssetInternal(relativePath, &presentFiles) && firstFailedPath.empty())
        {
            firstFailedPath = relativePath;
        }
    }

    SortAndRebuildIndexes();
    const std::size_t projectFileCount = static_cast<std::size_t>(std::ranges::count_if(
        mAssets,
        [](const std::unique_ptr<Asset>& asset)
        {
            return asset->GetType() == AssetType::ProjectSettings;
        }));
    if (!firstFailedPath.empty())
    {
        Diagnostics::Debug::LogError(
            "Asset database refresh failed because an asset could not be registered. root=",
            mProjectRootPath.string(), ", asset=", firstFailedPath.string());
        Clear();
        return false;
    }
    if (projectFileCount != 1)
    {
        Diagnostics::Debug::LogError(
            "Asset database requires exactly one root .gameproject file. root=",
            mProjectRootPath.string(), ", found=", projectFileCount);
        Clear();
        return false;
    }

    Diagnostics::Debug::Log(
        "Asset database refreshed. root=", mProjectRootPath.string(),
        ", assets=", mAssets.size());
    return true;
}

bool AssetDatabase::RegisterAsset(const std::filesystem::path& assetPath)
{
    if (mProjectRootPath.empty() || !RegisterAssetInternal(assetPath))
    {
        return false;
    }
    SortAndRebuildIndexes();
    return true;
}

bool AssetDatabase::RegisterAssetInternal(
    const std::filesystem::path& assetPath,
    const std::unordered_set<std::string>* const presentFiles)
{
    const std::optional<std::filesystem::path> relativePath = MakeRelativePath(assetPath);
    if (!relativePath)
    {
        Diagnostics::Debug::LogError(
            "Cannot register an asset outside the project root: ", assetPath.string());
        return false;
    }

    const std::optional<AssetType> assetType = GetAssetType(*relativePath);
    if (!assetType)
    {
        Diagnostics::Debug::LogError("Unsupported asset type: ", assetPath.string());
        return false;
    }

    // Read once so the hash, size and importer all describe the same bytes.
    // Separate reads can observe different states of a file edited during refresh.
    std::vector<std::byte> fileBytes;
    if (!mContentSource->Read(*relativePath, fileBytes))
    {
        Diagnostics::Debug::LogError("Failed to read asset data: ", relativePath->string());
        return false;
    }
    const std::uint64_t contentHash = Internal::HashBytes(fileBytes);
    const auto fileSize = static_cast<std::uintmax_t>(fileBytes.size());

    const std::string pathKey = MakePathKey(*relativePath);

    // 정체성이 조회 키를 정하므로 사이드카를 에셋보다 먼저 읽는다. 그러려면 사이드카 이름을
    // 알아야 하는데 그것은 형식이 답하는 것이라, 종류만으로 답할 수 있는 껍데기를 먼저 만들어
    // 이름을 묻는다. 이 껍데기는 이름을 묻는 데만 쓰이고 등록되지 않는다.
    const std::unique_ptr<Asset> probe = CreateAsset(
        0, *assetType, *relativePath, std::filesystem::path{}, 0, 0);
    if (!probe)
    {
        return false;
    }
    const std::optional<Core::Json> sidecar =
        ReadSidecar(*mContentSource, *probe, presentFiles);
    const Core::Guid guid = sidecar ? ReadGuid(*sidecar) : Core::Guid{};

    // guid가 있으면 그것이 정체성이고, 조회 키는 거기서 접어 만든다 — 파일이 옮겨져도 키가
    // 그대로인 이유가 이것이다. 없으면 경로에서 만든다: 그런 에셋은 guid로 가리킬 수 없고, 옮기면
    // 키가 바뀐다.
    const AssetKey assetId = guid.IsValid() ? guid.Fold() : HashPath(pathKey);
    if (!guid.IsValid())
    {
        Diagnostics::Debug::LogWarning(
            "This asset has no identity yet, so it can only be referred to by path. asset=",
            Internal::PathToUtf8(*relativePath));
    }
    std::unique_ptr<Asset> asset = CreateAsset(
        assetId, *assetType, *relativePath, mContentSource->ResolveFilePath(*relativePath),
        contentHash, fileSize);
    if (!asset)
    {
        return false;
    }
    asset->SetGuid(guid);
    // 형식만의 메타데이터는 형식이 채운다. 데이터베이스는 사이드카를 읽어 건네줄 뿐이다.
    if (sidecar)
    {
        try
        {
            asset->ReadSidecarMetadata(*sidecar);
        }
        catch (const std::exception& exception)
        {
            Diagnostics::Debug::LogError(
                "Failed to apply asset metadata; the asset is registered with defaults. asset=",
                Internal::PathToUtf8(*relativePath), ", error=", exception.what());
        }
    }


    // Importing is what turns a file into the assets inside it. It runs here, on refresh, rather
    // than when something first draws the file: the count and the names are what the editor lists
    // and what a reference is checked against, and both have to be known before a frame asks.
    std::vector<SubAsset> subAssets;
    std::string importError;
    if (const IAssetImporter* const importer = AssetImporterRegistry::Find(*relativePath))
    {
        const ImportIdentity identity{ assetId, contentHash };
        ImportedContents contents;
        if (importer->Import(
                *relativePath, fileBytes, identity, ImportMode::Structure, contents, importError))
        {
            asset->SetSubAssets(std::move(contents.subAssets));
        }
        else
        {
            // Registered with nothing inside it. A model the importer cannot read must not stop a
            // project from opening, and every reference into it fails on its own where it is used.
            Diagnostics::Debug::LogError(
                "Failed to import an asset's contents. path=", relativePath->string(),
                ", error=", importError);
        }
    }

    const auto existingPath = mAssetsByPath.find(pathKey);
    if (existingPath != mAssetsByPath.end())
    {
        // 내용이 바뀐 파일이다. 옛 페이로드가 남아 있으면 그 에셋은 계속 옛 내용을 답하므로
        // 여기서 잊는다 — 그리고 <b>교체보다 먼저</b> 잊어야 한다: 캐시의 키가 그 에셋 자신이라,
        // 교체한 뒤에는 잊을 대상을 가리킬 방법이 없다. 다음 로드가 다시 임포트하고, 옛
        // 페이로드를 아직 쥔 쪽은 스스로 그것을 살려 둔다.
        mPayloads.Forget(*mAssets[existingPath->second]);
        mAssets[existingPath->second] = std::move(asset);
        return true;
    }

    const auto existingId = mAssetsById.find(assetId);
    if (existingId != mAssetsById.end())
    {
        // guid가 있으면 이 충돌은 guid가 같다는 뜻이다(assetId는 guid를 접어 만들므로, 같은
        // guid는 언제나 여기서 걸린다 — 그 반대인 두 다른 guid가 우연히 같은 접힌 값을 내는
        // 경우도 이론상 있지만, 실제로 겪는 것은 언제나 앞엣것이다). guid가 없는 자산은 경로의
        // 해시가 우연히 부딪힌 것이라 다른 이야기이므로, 그때는 guid를 말하지 않는다.
        if (guid.IsValid())
        {
            Diagnostics::Debug::LogError(
                "Two assets claim the same identity; the second could not be registered. guid=",
                guid.ToString(), ", kept=",
                mAssets[existingId->second]->GetRelativePath().string(), ", ignored=",
                relativePath->string(),
                ". If one of these was renamed or moved, the other is likely a stale copy left "
                "behind in staged output — delete it, or do a clean build.");
        }
        else
        {
            Diagnostics::Debug::LogError(
                "Asset ID collision between ",
                mAssets[existingId->second]->GetRelativePath().string(), " and ",
                relativePath->string());
        }
        return false;
    }

    const std::size_t index = mAssets.size();
    mAssets.push_back(std::move(asset));
    mAssetsById.emplace(assetId, index);
    mAssetsByPath.emplace(pathKey, index);
    return true;
}

bool AssetDatabase::SaveManifest(const std::filesystem::path& manifestPath) const
{
    if (mProjectRootPath.empty())
    {
        Diagnostics::Debug::LogError("Cannot save an uninitialized asset database.");
        return false;
    }

    // 정체성 없는 에셋이 하나라도 있으면 여기서 멈춘다. 그대로 쓰면 매니페스트가 빈 guid를 담고,
    // 그것을 읽는 쪽은 "잘못된 기록"이라고만 말할 수 있다 — 어느 파일이 문제인지는 여기서만
    // 말할 수 있으므로 여기서 말한다.
    for (const std::unique_ptr<Asset>& asset : mAssets)
    {
        if (!asset->GetGuid().IsValid())
        {
            Diagnostics::Debug::LogError(
                "Cannot package an asset that has no identity yet; open the project in the editor "
                "so it is given one. asset=", Internal::PathToUtf8(asset->GetRelativePath()));
            return false;
        }
    }

    std::error_code error;
    const std::filesystem::path parentPath = manifestPath.parent_path();
    if (!parentPath.empty())
    {
        std::filesystem::create_directories(parentPath, error);
    }
    if (error)
    {
        Diagnostics::Debug::LogError(
            "Failed to create the asset database output directory: ", error.message());
        return false;
    }

    // 형식은 매니페스트가 안다. 표는 무엇을 적을지 — 이 에셋들을 — 만 말하고, 어떻게 적히는지는
    // 묻지 않는다.
    const Platform::FileWriteResult written =
        Platform::WriteTextFile(manifestPath, WriteManifestText(mAssets));
    if (!written)
    {
        Diagnostics::Debug::LogError(
            "Failed to write the asset database manifest: ", manifestPath.string(),
            ", reason=", written.Describe());
        return false;
    }
    return true;
}

bool AssetDatabase::LoadManifest(
    const Platform::IContentSource& source, const std::filesystem::path& manifestPath)
{
    Clear();
    mContentSource = &source;
    mProjectRootPath = source.GetDescription();

    std::vector<std::byte> manifestBytes;
    if (!source.Read(manifestPath, manifestBytes))
    {
        Diagnostics::Debug::LogError(
            "Failed to read the asset database manifest: ", manifestPath.string());
        Clear();
        return false;
    }

    // 파일을 읽어 레코드로 만드는 것은 매니페스트의 일이고, 그 레코드가 이 프로젝트와 맞는지
    // 보고 표에 넣는 것은 표의 일이다. 두 질문이 다르기 때문이다: 하나는 "이 파일이 제대로
    // 적혔는가"이고, 다른 하나는 "그 기록이 여기 있는 파일들을 가리키는가"이다.
    const std::optional<std::vector<ManifestRecord>> records = ReadManifestText(manifestBytes);
    if (!records)
    {
        Clear();
        return false;
    }

    for (const ManifestRecord& record : *records)
    {
        // 조회 키는 정체성에서 접어 만든다. 스캔이 하는 것과 같은 계산이라, 같은 프로젝트를
        // 스캔으로 열든 매니페스트로 열든 같은 키가 나온다.
        const AssetKey assetId = record.guid.Fold();
        const std::string pathKey = MakePathKey(record.relativePath);
        // Check existence here and verify the recorded content hash when each file is first loaded.
        // This keeps opening a project from reading every file or faulting the whole content pack
        // into memory before its assets are needed.
        if (!source.Exists(record.relativePath) ||
            mAssetsById.contains(assetId) || mAssetsByPath.contains(pathKey))
        {
            Diagnostics::Debug::LogError(
                "The asset database manifest does not match this project's files. asset=",
                Internal::PathToUtf8(record.relativePath));
            Clear();
            return false;
        }

        const std::size_t index = mAssets.size();
        std::unique_ptr<Asset> asset = CreateAsset(
            assetId, record.type, record.relativePath,
            source.ResolveFilePath(record.relativePath), record.contentHash, record.fileSize);
        if (!asset)
        {
            Diagnostics::Debug::LogError(
                "The asset database manifest names an asset class this build does not have. "
                "asset=", Internal::PathToUtf8(record.relativePath));
            Clear();
            return false;
        }
        asset->SetGuid(record.guid);
        // 형식만의 항목은 형식이 되읽는다. 손상된 기록은 여기서 던져 매니페스트 전체를 물린다 —
        // 잘못된 배율로 조용히 그리는 것보다 열리지 않는 편이 낫다.
        try
        {
            asset->ReadManifestMetadata(record.entry);
        }
        catch (const std::exception& exception)
        {
            Diagnostics::Debug::LogError(
                "Failed to read the asset database manifest: ", exception.what());
            Clear();
            return false;
        }
        // The manifest already says what this file holds, and the hash above says it still does,
        // so nothing is imported here. That is the whole value of shipping a manifest: a
        // packaged game reads its project without parsing a single model to open it.
        asset->SetSubAssets(record.subAssets);

        mAssets.push_back(std::move(asset));
        mAssetsById.emplace(assetId, index);
        mAssetsByPath.emplace(pathKey, index);
        mAssetsByGuid.emplace(record.guid, index);
    }

    SortAndRebuildIndexes();
    Diagnostics::Debug::Log(
        "Asset database loaded from manifest. assets=", mAssets.size(),
        ", root=", mProjectRootPath.string());
    return true;
}

const Asset* AssetDatabase::FindAsset(const AssetReference& reference) const
{
    if (!reference.IsValid())
    {
        return nullptr;
    }

    // 참조가 무엇으로 가리키는지에 따라 색인이 갈린다. 정체성으로 가리키면 파일이 옮겨져도
    // 닿고, 경로로 가리키면 그 자리에 있는 것에 닿는다.
    const Asset* const asset = reference.IsGuidReference()
        ? FindAsset(reference.GetGuid())
        : FindAsset(reference.GetPath());
    if (!asset || reference.GetLocalId() >= asset->GetSubAssets().size())
    {
        // A reference into a file that does not hold an asset at that local id names nothing. The
        // importer records what a file holds, so a stale reference — a mesh removed from a model, a
        // file replaced by one with fewer — fails here instead of drawing the wrong mesh.
        return nullptr;
    }
    return asset;
}

const Asset* AssetDatabase::FindSidecarOwner(const std::filesystem::path& relativePath) const
{
    const auto found = mAssetsBySidecarPath.find(MakePathKey(relativePath));
    return found == mAssetsBySidecarPath.end() ? nullptr : mAssets[found->second].get();
}

std::filesystem::path AssetDatabase::GetSidecarPath(const Asset& asset) const
{
    return mContentSource ? FindSidecarPath(*mContentSource, asset, nullptr)
                          : std::filesystem::path{};
}

const Asset* AssetDatabase::FindAsset(const AssetKey assetId) const
{
    const auto iterator = mAssetsById.find(assetId);
    return iterator == mAssetsById.end() ? nullptr : mAssets[iterator->second].get();
}

const Asset* AssetDatabase::FindAsset(const std::filesystem::path& assetPath) const
{
    const std::optional<std::filesystem::path> relativePath = MakeRelativePath(assetPath);
    if (!relativePath)
    {
        return nullptr;
    }
    const auto iterator = mAssetsByPath.find(MakePathKey(*relativePath));
    return iterator == mAssetsByPath.end() ? nullptr : mAssets[iterator->second].get();
}

const Asset* AssetDatabase::FindAsset(const Core::Guid& guid) const
{
    if (!guid.IsValid())
    {
        return nullptr;
    }
    const auto found = mAssetsByGuid.find(guid);
    return found == mAssetsByGuid.end() ? nullptr : mAssets[found->second].get();
}

std::optional<AssetType> AssetDatabase::GetAssetType(
    const std::filesystem::path& relativePath)
{
    const IAssetImporter* const importer = AssetImporterRegistry::Find(relativePath);
    if (!importer)
    {
        return std::nullopt;
    }

    // A `.gameproject` is the project's own settings file and exists once, at the root. One nested
    // in a content folder is not a second project, so it is not an asset at all.
    const AssetType assetType = importer->GetAssetType();
    if (assetType == AssetType::ProjectSettings && relativePath.has_parent_path())
    {
        return std::nullopt;
    }
    return assetType;
}

std::string_view AssetDatabase::GetAssetTypeName(const AssetType assetType)
{
    switch (assetType)
    {
    case AssetType::ProjectSettings:
        return "ProjectSettings";
    case AssetType::Scene:
        return "Scene";
    case AssetType::Sprite:
        return "Sprite";
    case AssetType::Mesh:
        return "Mesh";
    case AssetType::Font:
        return "Font";
    case AssetType::AudioClip:
        return "AudioClip";
    case AssetType::Icon:
        return "Icon";
    case AssetType::Skeleton:
        return "Skeleton";
    case AssetType::AnimationClip:
        return "AnimationClip";
    case AssetType::SkinnedMesh:
        return "SkinnedMesh";
    case AssetType::Material:
        return "Material";
    }
    return "Unknown";
}

std::optional<AssetType> AssetDatabase::ParseAssetTypeName(const std::string_view name)
{
    if (name == "ProjectSettings") return AssetType::ProjectSettings;
    if (name == "Scene") return AssetType::Scene;
    if (name == "Sprite") return AssetType::Sprite;
    if (name == "Mesh") return AssetType::Mesh;
    if (name == "Font") return AssetType::Font;
    if (name == "AudioClip") return AssetType::AudioClip;
    if (name == "Icon") return AssetType::Icon;
    if (name == "Skeleton") return AssetType::Skeleton;
    if (name == "AnimationClip") return AssetType::AnimationClip;
    if (name == "SkinnedMesh") return AssetType::SkinnedMesh;
    if (name == "Material") return AssetType::Material;
    return std::nullopt;
}

std::optional<std::filesystem::path> AssetDatabase::MakeRelativePath(
    const std::filesystem::path& assetPath) const
{
    // 규칙은 Platform에 하나다. 이 멤버가 남아 있는 것은 부르는 자리들이 이 데이터베이스의
    // 루트를 따로 알 필요가 없게 하기 위해서다.
    return Platform::RelativePathWithin(mProjectRootPath, assetPath);
}

void AssetDatabase::SortAndRebuildIndexes()
{
    std::ranges::sort(
        mAssets,
        [](const std::unique_ptr<Asset>& left, const std::unique_ptr<Asset>& right)
        {
            return MakePathKey(left->GetRelativePath()) < MakePathKey(right->GetRelativePath());
        });

    mAssetsById.clear();
    mAssetsByPath.clear();
    mAssetsByGuid.clear();
    mAssetsBySidecarPath.clear();
    for (std::size_t index = 0; index < mAssets.size(); ++index)
    {
        mAssetsById.emplace(mAssets[index]->GetId(), index);
        mAssetsByPath.emplace(MakePathKey(mAssets[index]->GetRelativePath()), index);
        if (const Core::Guid& guid = mAssets[index]->GetGuid(); guid.IsValid())
        {
            const auto [existing, inserted] = mAssetsByGuid.emplace(guid, index);
            if (!inserted)
            {
                // 두 에셋이 같은 정체성을 주장한다. 사이드카를 복사한 결과가 대부분이며, 조용히
                // 덮으면 둘 중 하나를 가리키는 모든 참조가 다른 것으로 해석된다.
                //
                // 가장 흔한 실제 원인은 복사다: 파일을 이름 바꾸거나 옮긴 뒤, 스테이징된 빌드
                // 출력에는 옛 사본이 그대로 남아 있고 새 사본이 곁에 생긴다. 스테이징은 더하기만
                // 하고 지우지 않으므로, 둘 다 같은 사이드카의 같은 guid를 갖고 남는다. 그래서
                // 사람이 다음에 할 일까지 여기서 말한다 — 어느 파일이 지금 진짜인지는 이 자리가
                // 모르지만, 스테이징을 비우고 새로 지으면 소스에 없는 사본은 다시 나타나지 않는다.
                Diagnostics::Debug::LogError(
                    "Two assets claim the same identity; the second is unreachable by it. guid=",
                    guid.ToString(), ", kept=", Internal::PathToUtf8(mAssets[existing->second]->GetRelativePath()),
                    ", ignored=", Internal::PathToUtf8(mAssets[index]->GetRelativePath()),
                    ". If one of these was renamed or moved, the other is likely a stale copy left "
                    "behind in staged output — delete it, or do a clean build.");
            }
        }
        // 두 이름 다 등록한다. 옛 이름의 사이드카도 그 에셋의 것이므로, 브라우저가 그것도 감춘다.
        for (const std::string_view suffix :
             { mAssets[index]->GetSidecarSuffix(), mAssets[index]->GetLegacySidecarSuffix() })
        {
            if (suffix.empty())
            {
                continue;
            }
            std::filesystem::path sidecarPath = mAssets[index]->GetRelativePath();
            sidecarPath += Internal::Utf8ToPath(std::string(suffix));
            mAssetsBySidecarPath.emplace(MakePathKey(sidecarPath), index);
        }
    }
}


std::shared_ptr<const MeshData> AssetDatabase::LoadMesh(const AssetReference& reference) const
{
    const Asset* const asset = FindAsset(reference);
    if (!asset)
    {
        return nullptr;
    }
    const LoadedFile* const loaded = mContentSource ? mPayloads.Load(*asset, *mContentSource) : nullptr;
    return loaded && reference.GetLocalId() < loaded->meshes.size()
        ? loaded->meshes[reference.GetLocalId()]
        : nullptr;
}

std::shared_ptr<const TextureData> AssetDatabase::LoadTexture(
    const AssetReference& reference) const
{
    const Asset* const asset = FindAsset(reference);
    if (!asset)
    {
        return nullptr;
    }
    const LoadedFile* const loaded = mContentSource ? mPayloads.Load(*asset, *mContentSource) : nullptr;
    return loaded && reference.GetLocalId() < loaded->images.size()
        ? loaded->images[reference.GetLocalId()]
        : nullptr;
}

std::shared_ptr<const AudioData> AssetDatabase::LoadAudioClip(
    const AssetReference& reference) const
{
    const Asset* const asset = FindAsset(reference);
    if (!asset)
    {
        return nullptr;
    }
    const LoadedFile* const loaded = mContentSource ? mPayloads.Load(*asset, *mContentSource) : nullptr;
    return loaded && reference.GetLocalId() < loaded->audioClips.size()
        ? loaded->audioClips[reference.GetLocalId()]
        : nullptr;
}

std::shared_ptr<const SkinnedMeshData> AssetDatabase::LoadSkinnedMesh(
    const AssetReference& reference) const
{
    const Asset* const asset = FindAsset(reference);
    if (!asset)
    {
        return nullptr;
    }
    const LoadedFile* const loaded = mContentSource ? mPayloads.Load(*asset, *mContentSource) : nullptr;
    return loaded && reference.GetLocalId() < loaded->skinnedMeshes.size()
        ? loaded->skinnedMeshes[reference.GetLocalId()]
        : nullptr;
}

std::shared_ptr<const Animation::Skeleton> AssetDatabase::LoadSkeleton(
    const AssetReference& reference) const
{
    const Asset* const asset = FindAsset(reference);
    if (!asset)
    {
        return nullptr;
    }
    const LoadedFile* const loaded = mContentSource ? mPayloads.Load(*asset, *mContentSource) : nullptr;
    return loaded && reference.GetLocalId() < loaded->skeletons.size()
        ? loaded->skeletons[reference.GetLocalId()]
        : nullptr;
}

std::shared_ptr<const Animation::AnimationClip> AssetDatabase::LoadAnimationClip(
    const AssetReference& reference) const
{
    const Asset* const asset = FindAsset(reference);
    if (!asset)
    {
        return nullptr;
    }
    const LoadedFile* const loaded = mContentSource ? mPayloads.Load(*asset, *mContentSource) : nullptr;
    return loaded && reference.GetLocalId() < loaded->animationClips.size()
        ? loaded->animationClips[reference.GetLocalId()]
        : nullptr;
}

std::shared_ptr<const MaterialData> AssetDatabase::LoadMaterial(
    const AssetReference& reference) const
{
    const Asset* const asset = FindAsset(reference);
    if (!asset)
    {
        return nullptr;
    }
    const LoadedFile* const loaded = mContentSource ? mPayloads.Load(*asset, *mContentSource) : nullptr;
    return loaded && reference.GetLocalId() < loaded->materials.size()
        ? loaded->materials[reference.GetLocalId()]
        : nullptr;
}


void AssetDatabase::UnloadUnreferenced(const std::unordered_set<AssetKey>& referencedAssets)
{
    mPayloads.UnloadUnreferenced(referencedAssets);
}

void AssetDatabase::InheritPayloadsFrom(const AssetDatabase& previous)
{
    if (&previous == this)
    {
        return;
    }

    // 같은 파일인지 정하는 일은 표의 것이다: 정체성이 같고 내용 해시도 같아야 같은 파일이다.
    // 캐시는 그 답을 받아 옮기기만 한다 — 무엇이 같은 파일인지 두 곳이 각자 판단하면 그 둘은
    // 언젠가 갈린다.
    for (const std::unique_ptr<Asset>& asset : mAssets)
    {
        if (!asset || !asset->GetGuid().IsValid())
        {
            continue;
        }
        const auto matched = previous.mAssetsByGuid.find(asset->GetGuid());
        if (matched == previous.mAssetsByGuid.end())
        {
            continue;
        }
        const Asset* const before = previous.mAssets[matched->second].get();
        if (!before || before->GetContentHash() != asset->GetContentHash())
        {
            continue;
        }
        static_cast<void>(mPayloads.AdoptFrom(previous.mPayloads, *before, *asset));
    }
}

std::size_t AssetDatabase::GetLoadedPayloadCount() const
{
    return mPayloads.GetLoadedCount();
}


const Platform::IContentSource& AssetDatabase::GetContentSource() const
{
    // A database that was never refreshed still has to answer, because a caller holding one has no
    // other way to read a file. An empty source holds nothing and every read from it fails, which is
    // the same answer an unrefreshed database gives to every other question.
    static const Platform::DirectoryContentSource emptySource{ std::filesystem::path{} };
    return mContentSource ? *mContentSource : emptySource;
}

void AssetDatabase::Clear()
{
    mPayloads.Clear();
    mAssets.clear();
    mAssetsById.clear();
    mAssetsByPath.clear();
    mAssetsByGuid.clear();
    mAssetsBySidecarPath.clear();
    mProjectRootPath.clear();
    mContentSource = nullptr;
}


AssetReference MakeAssetReference(const Asset& asset, const AssetReferenceForm form)
{
    // 정체성을 달라고 했는데 그 에셋에게 정체성이 없으면 경로로 돌아간다. 가리키지
    // 못하는 참조를 만드는 것보다 한 형식 섮이는 쪽이 싸다.
    return form == AssetReferenceForm::Identity && asset.GetGuid().IsValid()
        ? AssetReference(asset.GetGuid())
        : AssetReference(asset.GetRelativePath());
}

AssetReferenceStatus ClassifyAssetReference(
    const AssetDatabase& database, const AssetReference& reference)
{
    if (!reference.IsValid())
    {
        return AssetReferenceStatus::Empty;
    }
    return database.FindAsset(reference) != nullptr ? AssetReferenceStatus::Resolved
                                                    : AssetReferenceStatus::Missing;
}

std::string DescribeAssetReference(
    const AssetDatabase& database, const AssetReference& reference)
{
    if (!reference.IsValid())
    {
        return "(None)";
    }
    const Asset* const asset = database.FindAsset(reference);
    if (!asset)
    {
        return reference.ToString();
    }
    std::string text = Internal::PathToUtf8(asset->GetRelativePath());
    const std::vector<SubAsset>& subAssets = asset->GetSubAssets();
    const std::size_t localId = reference.GetLocalId();
    // 파일이 하나만 담으면 경로가 곧 이름이다. 여럿이면 그중 어느 것인지 말해야 한다.
    if (subAssets.size() > 1 && localId < subAssets.size())
    {
        text += '#';
        text += std::to_string(localId);
        text += " (";
        text += subAssets[localId].name;
        text += ")";
    }
    return text;
}

bool AssetReferenceMatchesType(
    const AssetDatabase& database,
    const AssetReference& reference,
    const std::optional<AssetType> assetType)
{
    const Asset* const asset = database.FindAsset(reference);
    if (!asset)
    {
        return false;
    }
    if (!assetType)
    {
        return true;
    }
    // 파일의 대표 타입이 아니라 참조가 가리키는 서브에셋의 타입을 본다. 한 모델 파일은
    // 메시와 스켈레톤처럼 서로 다른 타입을 함께 낼 수 있다. CollectAssetChoices와 같은
    // 규칙을 사용해야 선택 목록이 제시하는 참조를 여기서도 받아들인다.
    const std::vector<SubAsset>& subAssets = asset->GetSubAssets();
    const std::size_t localId = reference.GetLocalId();
    if (localId < subAssets.size())
    {
        return subAssets[localId].type == *assetType;
    }
    return asset->GetType() == *assetType;
}

std::vector<AssetChoice> CollectAssetChoices(
    const AssetDatabase& database, const AssetType type, const AssetReferenceForm form)
{
    std::vector<AssetChoice> choices;
    // 등록 목록은 경로 순으로 유지되므로 그대로 걸으면 선택지도 경로 순이다.
    for (const std::unique_ptr<Asset>& asset : database.GetAssets())
    {
        const std::vector<SubAsset>& subAssets = asset->GetSubAssets();
        const std::string path = Internal::PathToUtf8(asset->GetRelativePath());
        for (std::size_t localId = 0; localId < subAssets.size(); ++localId)
        {
            if (subAssets[localId].type != type)
            {
                continue;
            }
            AssetChoice choice;
            const auto localIdValue = static_cast<std::uint32_t>(localId);
            choice.reference = form == AssetReferenceForm::Identity && asset->GetGuid().IsValid()
                ? AssetReference(asset->GetGuid(), localIdValue)
                : AssetReference(asset->GetRelativePath(), localIdValue);
            // 파일이 에셋 하나만 담으면 경로가 곧 이름이다. 여럿이면 어느 것인지 말해야 한다.
            // 목록에 보이는 것은 사람이 읽는 이름이지 저장 형식이 아니다.
            choice.label = subAssets.size() == 1
                ? path
                : path + "#" + std::to_string(localId) + " (" + subAssets[localId].name + ")";
            choices.push_back(std::move(choice));
        }
    }
    return choices;
}

}
