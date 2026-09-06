#include "pch.h"
#include "AssetPayloadCache.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <ranges>
#include <vector>

#include "AssetImporter.h"
#include "AssetImporterRegistry.h"
#include "AssetDatabaseInternal.h"
#include "../Diagnostics/Debug.h"
#include "../Platform/IContentSource.h"

namespace GameEngine::Assets
{


bool LoadedFile::IsAnyPayloadHeldElsewhere() const
{
    bool held = false;
    ForEachPayloadRange(
        [&held](const auto& payloads)
        {
            held = held ||
                std::ranges::any_of(
                    payloads, [](const auto& payload) { return payload.use_count() > 1; });
        });
    return held;
}

const LoadedFile* AssetPayloadCache::Load(
    const Asset& asset, const Platform::IContentSource& source) const
{
    if (const auto loaded = mFiles.find(&asset); loaded != mFiles.end())
    {
        return &loaded->second;
    }
    const std::filesystem::path& relativePath = asset.GetRelativePath();
    const IAssetImporter* const importer = AssetImporterRegistry::Find(relativePath);
    if (!importer)
    {
        return nullptr;
    }

    std::vector<std::byte> fileBytes;
    if (!source.Read(relativePath, fileBytes))
    {
        Diagnostics::Debug::LogError(
            "Failed to read an asset to load it. path=", relativePath.string());
        return nullptr;
    }

    // The bytes must be the ones the registration described, or a reference recorded against the
    // file now names a different asset. Checked here, on the first load, rather than for every file
    // when a manifest is opened: this is the one moment the bytes are in hand anyway, so the check
    // costs a hash of data already read instead of a read of every file in the project.
    if (static_cast<std::uintmax_t>(fileBytes.size()) != asset.GetFileSize() ||
        Internal::HashBytes(fileBytes) != asset.GetContentHash())
    {
        Diagnostics::Debug::LogError(
            "An asset's bytes differ from when it was registered. path=", relativePath.string());
        return nullptr;
    }

    ImportedContents contents;
    std::string importError;
    const ImportIdentity identity{ asset.GetId(), asset.GetContentHash() };
    if (!importer->Import(
            relativePath, fileBytes, identity, ImportMode::Full, contents, importError))
    {
        Diagnostics::Debug::LogError(
            "Failed to load an asset. path=", relativePath.string(), ", error=", importError);
        return nullptr;
    }

    // What the file holds must not have changed since it was scanned, or a reference recorded
    // against it now names a different asset.
    if (contents.subAssets.size() != asset.GetSubAssets().size())
    {
        Diagnostics::Debug::LogError(
            "An asset holds different contents than when it was registered. path=",
            relativePath.string());
        return nullptr;
    }

    // A file can mix static meshes, skinned meshes, skeletons and clips. Every payload vector
    // has one slot per sub-asset, so its size counts slots across all kinds. Count non-null entries
    // to report the number of payloads of each kind.
    const auto countLoaded = [](const auto& payloads)
    {
        return std::ranges::count_if(payloads, [](const auto& payload) { return payload != nullptr; });
    };
    Diagnostics::Debug::Log(
        "Loaded an asset. path=", relativePath.string(),
        ", meshes=", countLoaded(contents.meshes), ", images=", countLoaded(contents.images),
        ", audioClips=", countLoaded(contents.audioClips),
        ", skinnedMeshes=", countLoaded(contents.skinnedMeshes),
        ", skeletons=", countLoaded(contents.skeletons),
        ", animationClips=", countLoaded(contents.animationClips),
        ", materials=", countLoaded(contents.materials));
    return &mFiles
                .insert_or_assign(
                    &asset,
                    LoadedFile{ std::move(contents.meshes), std::move(contents.images),
                        std::move(contents.audioClips), std::move(contents.skinnedMeshes),
                        std::move(contents.skeletons), std::move(contents.animationClips),
                        std::move(contents.materials) })
                .first->second;
}

void AssetPayloadCache::UnloadUnreferenced(const std::unordered_set<AssetKey>& referencedAssets)
{
    const std::size_t before = mFiles.size();
    std::erase_if(
        mFiles,
        [&referencedAssets](const auto& entry)
        {
            // Whether an asset is wanted is the caller's answer, not this map's. Deciding it here
            // from the reference count could not work: this map is the only thing that holds a
            // payload between frames by design, so every loaded asset looks unused from in here —
            // including the ones a running scene draws every frame.
            if (referencedAssets.contains(entry.first->GetId()))
            {
                return false;
            }

            // Wanted by nobody is still not enough. A frame in flight, a backend's resolved
            // resource and a playing audio voice each hold a payload, and freeing one out from
            // under any of them is a crash rather than a saving; those are collected on a later
            // sweep once they let go. Which payload kinds exist is LoadedFile's answer, not this
            // sweep's, so every kind participates in the ownership check.
            return !entry.second.IsAnyPayloadHeldElsewhere();
        });
    if (before != mFiles.size())
    {
        Diagnostics::Debug::Log(
            "Unloaded assets nothing references. files=", before - mFiles.size(),
            ", remaining=", mFiles.size());
    }
}

bool AssetPayloadCache::AdoptFrom(
    const AssetPayloadCache& previous, const Asset& before, const Asset& after)
{
    const auto loaded = previous.mFiles.find(&before);
    if (loaded == previous.mFiles.end())
    {
        return false;
    }
    mFiles[&after] = loaded->second;
    return true;
}

}
