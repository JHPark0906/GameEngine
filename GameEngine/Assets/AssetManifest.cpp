#include "pch.h"
#include "AssetManifest.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <sstream>
#include <string_view>

#include "AssetDatabase.h"
#include "AssetDatabaseInternal.h"
#include "../Platform/RelativePath.h"
#include "../Diagnostics/Debug.h"

namespace GameEngine::Assets
{

namespace
{
    /// <summary>
    /// JSON 문자열 하나를 따옴표와 이스케이프까지 적는다. 형식이 아는 일이므로 여기 있다.
    /// </summary>
    void WriteEscapedJsonString(std::ostream& stream, const std::string_view text)
    {
        stream << '"';
        for (const char character : text)
        {
            switch (character)
            {
            case '"': stream << "\\\""; break;
            case '\\': stream << "\\\\"; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default:
                if (static_cast<unsigned char>(character) < 0x20)
                {
                    stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<unsigned int>(static_cast<unsigned char>(character))
                           << std::dec << std::setfill(' ');
                }
                else
                {
                    stream << character;
                }
                break;
            }
        }
        stream << '"';
    }

    /// <summary>64비트 값을 자릿수 고정 16진수로 적는다. 폭이 고정이라 바이트가 흔들리지 않는다.</summary>
    [[nodiscard]] std::string ToHex(const std::uint64_t value)
    {
        std::ostringstream stream;
        stream << std::hex << std::setfill('0') << std::setw(16) << value;
        return stream.str();
    }

    [[nodiscard]] std::optional<std::uint64_t> ParseHex(const std::string& text)
    {
        std::uint64_t value = 0;
        const char* const first = text.data();
        const char* const last = first + text.size();
        const std::from_chars_result result = std::from_chars(first, last, value, 16);
        if (result.ec != std::errc{} || result.ptr != last)
        {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] std::optional<std::uintmax_t> ParseFileSize(const std::string& text)
    {
        std::uintmax_t value = 0;
        const char* const first = text.data();
        const char* const last = first + text.size();
        const std::from_chars_result result = std::from_chars(first, last, value);
        if (result.ec != std::errc{} || result.ptr != last)
        {
            return std::nullopt;
        }
        return value;
    }
}

std::string WriteManifestText(const std::span<const std::unique_ptr<Asset>> assets)
{
    // 매니페스트를 먼저 문자열로 엮고 부르는 쪽이 한 번 쓴다. 쓰는 자리가 하나여야 실패를 한 번만
    // 묻고, 쓰는 방식을 바꾸는 일도 한 줄이 된다.
    std::ostringstream stream;

    stream << "{\n  \"formatVersion\": " << AssetDatabase::CurrentFormatVersion
           << ",\n  \"assets\": [";
    for (std::size_t index = 0; index < assets.size(); ++index)
    {
        const Asset& asset = *assets[index];
        stream << (index == 0 ? "\n" : ",\n") << "    {\n      \"guid\": ";
        WriteEscapedJsonString(stream, asset.GetGuid().ToString());
        stream << ",\n      \"type\": ";
        WriteEscapedJsonString(stream, AssetDatabase::GetAssetTypeName(asset.GetType()));
        stream << ",\n      \"path\": ";
        WriteEscapedJsonString(stream, Internal::PathToUtf8(asset.GetRelativePath()));
        stream << ",\n      \"contentHash\": ";
        WriteEscapedJsonString(stream, ToHex(asset.GetContentHash()));
        stream << ",\n      \"fileSize\": ";
        WriteEscapedJsonString(stream, std::to_string(asset.GetFileSize()));
        // The assets inside the file, in local-id order. A deployed player reads this instead of
        // reimporting, so a model is parsed once at build time rather than on every launch.
        stream << ",\n      \"subAssets\": [";
        const std::vector<SubAsset>& subAssets = asset.GetSubAssets();
        for (std::size_t subAssetIndex = 0; subAssetIndex < subAssets.size(); ++subAssetIndex)
        {
            const SubAsset& subAsset = subAssets[subAssetIndex];
            stream << (subAssetIndex == 0 ? "\n        {\"type\": " : ",\n        {\"type\": ");
            WriteEscapedJsonString(stream, AssetDatabase::GetAssetTypeName(subAsset.type));
            stream << ", \"name\": ";
            WriteEscapedJsonString(stream, subAsset.name);
            stream << "}";
        }
        stream << (subAssets.empty() ? "]" : "\n      ]");
        // 형식만의 항목은 형식이 답한다. 매니페스트는 그것을 싣는 방법만 알고, 무엇이 실리는지는
        // 모른다 — 새 형식이 메타데이터를 갖게 되어도 여기는 그대로다.
        Core::Json::Object extraMembers;
        asset.WriteManifestMetadata(extraMembers);
        std::vector<const std::string*> extraKeys;
        extraKeys.reserve(extraMembers.size());
        for (const auto& member : extraMembers)
        {
            extraKeys.push_back(&member.first);
        }
        // 매니페스트는 빌드 산출물이라 실행마다 같은 바이트여야 한다. Object는 순서가 없으므로
        // 이름순으로 적는다.
        std::ranges::sort(extraKeys, {}, [](const std::string* key) -> const std::string&
        {
            return *key;
        });
        for (const std::string* key : extraKeys)
        {
            stream << ",\n      ";
            WriteEscapedJsonString(stream, *key);
            stream << ": " << extraMembers.at(*key).Dump();
        }
        stream << "\n    }";
    }
    stream << (assets.empty() ? "" : "\n") << "  ]\n}\n";
    return stream.str();
}

std::optional<std::vector<ManifestRecord>> ReadManifestText(
    const std::span<const std::byte> bytes)
{
    try
    {
        const Core::Json root = Core::Json::ParseBytes(bytes);
        if (root.At("formatVersion").Get<unsigned int>() != AssetDatabase::CurrentFormatVersion)
        {
            throw Core::JsonError("unsupported asset database format version");
        }

        std::vector<ManifestRecord> records;
        for (const Core::Json& assetJson : root.At("assets").AsArray())
        {
            // 매니페스트는 빌드가 쓴 것이라 정체성이 반드시 있다. 없으면 손상이므로 거절한다 —
            // 조용히 경로로 되돌아가면 패키지가 guid 참조를 하나도 풀지 못한 채 실행된다.
            const std::optional<Core::Guid> guid =
                Core::Guid::Parse(assetJson.At("guid").Get<std::string>());
            const std::optional<AssetType> type = AssetDatabase::ParseAssetTypeName(
                assetJson.At("type").Get<std::string>());
            const std::filesystem::path relativePath = Internal::Utf8ToPath(
                assetJson.At("path").Get<std::string>()).lexically_normal();
            const std::optional<std::uint64_t> contentHash = ParseHex(
                assetJson.At("contentHash").Get<std::string>());
            const std::optional<std::uintmax_t> fileSize = ParseFileSize(
                assetJson.At("fileSize").Get<std::string>());
            if (!guid || !type || !contentHash || !fileSize || relativePath.empty() ||
                relativePath.is_absolute() || Platform::EscapesRoot(relativePath) ||
                AssetDatabase::GetAssetType(relativePath) != type)
            {
                throw Core::JsonError("invalid asset database record");
            }

            // Read back what the importer found at build time rather than reimporting: a deployed
            // player should not have to parse a model file to know how many meshes it holds.
            std::vector<SubAsset> subAssets;
            for (const Core::Json& subAssetJson : assetJson.At("subAssets").AsArray())
            {
                const std::optional<AssetType> subAssetType = AssetDatabase::ParseAssetTypeName(
                    subAssetJson.At("type").Get<std::string>());
                if (!subAssetType)
                {
                    throw Core::JsonError("invalid sub-asset type");
                }
                subAssets.push_back({ *subAssetType, subAssetJson.At("name").Get<std::string>() });
            }

            records.push_back(ManifestRecord{
                *guid, *type, relativePath, *contentHash, *fileSize, std::move(subAssets),
                assetJson });
        }
        return records;
    }
    catch (const std::exception& exception)
    {
        Diagnostics::Debug::LogError(
            "Failed to read the asset database manifest: ", exception.what());
        return std::nullopt;
    }
}

}
