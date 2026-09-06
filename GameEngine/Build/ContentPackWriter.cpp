#include "pch.h"
#include "ContentPackWriter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "../Platform/ContentPackFormat.h"
#include "../Diagnostics/Debug.h"

namespace GameEngine::Build
{

namespace
{
    namespace Format = Platform::ContentPackFormat;

    void Append(std::vector<std::byte>& buffer, const std::byte* const bytes, const std::size_t size)
    {
        buffer.insert(buffer.end(), bytes, bytes + size);
    }

    template <std::size_t Size>
    void Append(std::vector<std::byte>& buffer, const std::array<std::byte, Size>& bytes)
    {
        Append(buffer, bytes.data(), bytes.size());
    }

    [[nodiscard]] std::string ToPackPath(const std::filesystem::path& relativePath)
    {
        const std::u8string generic = relativePath.lexically_normal().generic_u8string();
        return {
            reinterpret_cast<const char*>(generic.data()),
            reinterpret_cast<const char*>(generic.data() + generic.size())
        };
    }
}

bool AppendContentPack(
    const std::filesystem::path& executablePath,
    const std::filesystem::path& contentRootPath,
    const std::vector<std::filesystem::path>& relativePaths)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(executablePath, error) || error)
    {
        Diagnostics::Debug::LogError(
            "Cannot pack content into a missing executable. path=", executablePath.string());
        return false;
    }

    // The whole pack is assembled in memory before a byte of it reaches the executable, so a failure
    // part-way through leaves a working executable rather than one with half a pack stuck to it.
    std::vector<std::byte> data;
    std::vector<std::byte> index;
    std::uint32_t entryCount = 0;
    for (const std::filesystem::path& relativePath : relativePaths)
    {
        const std::filesystem::path absolutePath = contentRootPath / relativePath;
        std::ifstream input(absolutePath, std::ios::binary | std::ios::ate);
        if (!input)
        {
            Diagnostics::Debug::LogError(
                "Failed to open a file to pack. path=", absolutePath.string());
            return false;
        }
        const std::streamoff size = input.tellg();
        if (size < 0)
        {
            Diagnostics::Debug::LogError(
                "Failed to size a file to pack. path=", absolutePath.string());
            return false;
        }

        // Store offsets relative to the pack, not the executable. The same index remains
        // valid regardless of how large the player binary is in this configuration.
        const std::uint64_t offset = data.size();
        data.resize(data.size() + static_cast<std::size_t>(size));
        input.seekg(0);
        if (size > 0 &&
            !input.read(
                reinterpret_cast<char*>(data.data() + offset), static_cast<std::streamsize>(size)))
        {
            Diagnostics::Debug::LogError(
                "Failed to read a file to pack. path=", absolutePath.string());
            return false;
        }

        const std::string packPath = ToPackPath(relativePath);
        if (packPath.empty() || packPath.size() > Format::MaximumPathLength)
        {
            Diagnostics::Debug::LogError(
                "A packed file's path is empty or too long. path=", relativePath.string());
            return false;
        }
        Append(index, Format::WriteUInt32(static_cast<std::uint32_t>(packPath.size())));
        Append(index, reinterpret_cast<const std::byte*>(packPath.data()), packPath.size());
        Append(index, Format::WriteUInt64(offset));
        Append(index, Format::WriteUInt64(static_cast<std::uint64_t>(size)));
        ++entryCount;
    }

    if (entryCount > Format::MaximumEntryCount)
    {
        Diagnostics::Debug::LogError(
            "A content pack may not hold this many files. files=", entryCount);
        return false;
    }

    // The reader starts at the file's footer and uses packSize to find the appended block;
    // preserve this field order together with Platform::ContentPackFormat and its reader.
    const std::uint64_t indexOffset = data.size();
    const std::uint64_t packSize = indexOffset + index.size() + Format::FooterSize;
    Append(data, index.data(), index.size());
    Append(data, Format::WriteUInt32(entryCount));
    Append(data, Format::WriteUInt64(indexOffset));
    Append(data, Format::WriteUInt64(packSize));
    Append(data, reinterpret_cast<const std::byte*>(Format::Magic.data()), Format::Magic.size());

    std::ofstream output(executablePath, std::ios::binary | std::ios::app);
    if (!output)
    {
        Diagnostics::Debug::LogError(
            "Failed to open the executable to pack into. path=", executablePath.string());
        return false;
    }
    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    output.flush();
    if (!output)
    {
        Diagnostics::Debug::LogError(
            "Failed to append the content pack. path=", executablePath.string());
        return false;
    }

    Diagnostics::Debug::Log(
        "Packed content into the executable. path=", executablePath.string(),
        ", files=", entryCount, ", bytes=", data.size());
    return true;
}

}
