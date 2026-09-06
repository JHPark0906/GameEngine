#include "pch.h"
#include "PackedContentSource.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ContentPackFormat.h"
#include "PlatformServices.h"
#include "../Diagnostics/Debug.h"

namespace GameEngine::Platform
{

namespace
{
    /// <summary>
    /// 팩 안에서 경로가 키가 되는 방식이다: 슬래시, 소문자. 대소문자를 보존하는 파일시스템에서
    /// 만들어진 팩도 이름 철자가 다른 조회로 읽혀야 하는데, 에셋 데이터베이스가 하는 허용과
    /// 같은 것이다.
    /// </summary>
    [[nodiscard]] std::string MakeKey(const std::filesystem::path& path)
    {
        const std::u8string generic = path.lexically_normal().generic_u8string();
        std::string key(
            reinterpret_cast<const char*>(generic.data()),
            reinterpret_cast<const char*>(generic.data() + generic.size()));
        std::ranges::transform(
            key,
            key.begin(),
            [](const unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        return key;
    }
}

PackedContentSource::PackedContentSource(std::filesystem::path executablePath)
    : mExecutablePath(std::move(executablePath))
{
    mMapping = PlatformServices::MapFileReadOnly(mExecutablePath);
    mIsValid = ReadIndex();
}

std::span<const std::byte> PackedContentSource::GetPackBytes() const
{
    return mMapping ? mMapping->GetBytes() : std::span<const std::byte>{};
}

bool PackedContentSource::ReadIndex()
{
    const std::span<const std::byte> file = GetPackBytes();
    if (file.size() < ContentPackFormat::FooterSize)
    {
        return false;
    }

    const std::byte* const footer = file.data() + file.size() - ContentPackFormat::FooterSize;

    // No magic means no pack, which is the ordinary case for a development build and not a failure.
    const std::byte* const magic = footer + 4 + 8 + 8;
    if (std::memcmp(magic, ContentPackFormat::Magic.data(), ContentPackFormat::Magic.size()) != 0)
    {
        return false;
    }

    const std::uint32_t entryCount = ContentPackFormat::ReadUInt32(footer);
    const std::uint64_t indexOffset = ContentPackFormat::ReadUInt64(footer + 4);
    const std::uint64_t packSize = ContentPackFormat::ReadUInt64(footer + 4 + 8);
    // The index must fit between its offset and the footer. Reject offsets inside the footer
    // before subtracting so the index size cannot underflow into an invalid span.
    if (entryCount > ContentPackFormat::MaximumEntryCount || packSize > file.size() ||
        indexOffset >= packSize || packSize - indexOffset < ContentPackFormat::FooterSize)
    {
        Diagnostics::Debug::LogError(
            "The content pack footer does not describe this file. path=", mExecutablePath.string());
        return false;
    }
    mPackOffset = file.size() - packSize;

    const std::uint64_t indexSize = packSize - indexOffset - ContentPackFormat::FooterSize;
    const std::span<const std::byte> index =
        file.subspan(static_cast<std::size_t>(mPackOffset + indexOffset),
                     static_cast<std::size_t>(indexSize));

    std::size_t position = 0;
    mPaths.reserve(entryCount);
    for (std::uint32_t entry = 0; entry < entryCount; ++entry)
    {
        if (position + 4 > index.size())
        {
            break;
        }
        const std::uint32_t pathLength = ContentPackFormat::ReadUInt32(index.data() + position);
        position += 4;
        if (pathLength == 0 || pathLength > ContentPackFormat::MaximumPathLength ||
            position + pathLength + 16 > index.size())
        {
            break;
        }

        const std::u8string pathText(
            reinterpret_cast<const char8_t*>(index.data() + position), pathLength);
        position += pathLength;
        const std::uint64_t offset = ContentPackFormat::ReadUInt64(index.data() + position);
        position += 8;
        const std::uint64_t size = ContentPackFormat::ReadUInt64(index.data() + position);
        position += 8;
        if (offset > indexOffset || size > indexOffset - offset)
        {
            break;
        }

        std::filesystem::path relativePath(pathText);
        mEntries.insert_or_assign(MakeKey(relativePath), Entry{ offset, size });
        mPaths.push_back(std::move(relativePath));
    }

    if (mPaths.size() != entryCount)
    {
        Diagnostics::Debug::LogError(
            "The content pack index is truncated or malformed. path=", mExecutablePath.string(),
            ", read=", mPaths.size(), ", expected=", entryCount);
        mEntries.clear();
        mPaths.clear();
        return false;
    }

    Diagnostics::Debug::Log(
        "Reading content from the executable. path=", mExecutablePath.string(),
        ", files=", mPaths.size());
    return true;
}

bool PackedContentSource::Exists(const std::filesystem::path& relativePath) const
{
    return mIsValid && mEntries.contains(MakeKey(relativePath));
}

bool PackedContentSource::Read(
    const std::filesystem::path& relativePath, std::vector<std::byte>& bytes) const
{
    bytes.clear();
    if (!mIsValid)
    {
        return false;
    }

    const auto entry = mEntries.find(MakeKey(relativePath));
    if (entry == mEntries.end())
    {
        return false;
    }
    if (entry->second.size == 0)
    {
        return true;
    }

    // A copy out of the mapping. The pages it touches are faulted in here and are the operating
    // system's to keep or drop afterwards; nothing here decides how much of the pack stays resident.
    const std::span<const std::byte> file = GetPackBytes();
    const std::size_t offset = static_cast<std::size_t>(mPackOffset + entry->second.offset);
    const auto size = static_cast<std::size_t>(entry->second.size);
    if (offset + size > file.size())
    {
        Diagnostics::Debug::LogError(
            "A content pack entry lies outside the file. path=", relativePath.string());
        return false;
    }
    bytes.assign(file.begin() + offset, file.begin() + offset + size);
    return true;
}

std::vector<std::filesystem::path> PackedContentSource::List() const
{
    return mPaths;
}

}
