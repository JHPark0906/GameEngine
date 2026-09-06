#include "pch.h"
#include "DirectoryContentSource.h"

#include "../Core/RelativePath.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <system_error>
#include <utility>
#include <vector>

namespace GameEngine::Platform
{

namespace
{
}

DirectoryContentSource::DirectoryContentSource(std::filesystem::path rootPath)
{
    std::error_code error;
    mRootPath = std::filesystem::weakly_canonical(rootPath, error);
    if (error)
    {
        mRootPath = std::move(rootPath);
        return;
    }
    mIsValid = std::filesystem::is_directory(mRootPath, error) && !error;
}

std::filesystem::path DirectoryContentSource::ResolveFilePath(
    const std::filesystem::path& relativePath) const
{
    if (!mIsValid || relativePath.empty() || relativePath.is_absolute() ||
        Core::EscapesRoot(relativePath))
    {
        return {};
    }

    // Normalising is not enough on its own: a symbolic link inside the root can still point out of
    // it, so the resolved path is checked against the root rather than trusted for looking tidy.
    std::error_code error;
    const std::filesystem::path absolutePath =
        std::filesystem::weakly_canonical(mRootPath / relativePath, error);
    if (error)
    {
        return {};
    }

    const std::filesystem::path relativeToRoot = absolutePath.lexically_relative(mRootPath);
    if (relativeToRoot.empty() || Core::EscapesRoot(relativeToRoot))
    {
        return {};
    }
    return absolutePath;
}

bool DirectoryContentSource::Exists(const std::filesystem::path& relativePath) const
{
    const std::filesystem::path absolutePath = ResolveFilePath(relativePath);
    if (absolutePath.empty())
    {
        return false;
    }
    std::error_code error;
    return std::filesystem::is_regular_file(absolutePath, error) && !error;
}

bool DirectoryContentSource::Read(
    const std::filesystem::path& relativePath, std::vector<std::byte>& bytes) const
{
    bytes.clear();

    const std::filesystem::path absolutePath = ResolveFilePath(relativePath);
    if (absolutePath.empty())
    {
        return false;
    }

    std::ifstream stream(absolutePath, std::ios::binary | std::ios::ate);
    if (!stream)
    {
        return false;
    }
    const std::streamoff length = stream.tellg();
    if (length < 0)
    {
        return false;
    }

    bytes.resize(static_cast<std::size_t>(length));
    if (bytes.empty())
    {
        return true;
    }
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), length))
    {
        bytes.clear();
        return false;
    }
    return true;
}

std::vector<std::filesystem::path> DirectoryContentSource::List() const
{
    std::vector<std::filesystem::path> paths;
    if (!mIsValid)
    {
        return paths;
    }

    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        mRootPath, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end)
    {
        const std::filesystem::directory_entry entry = *iterator;
        iterator.increment(error);
        if (!entry.is_regular_file())
        {
            continue;
        }

        std::error_code relativeError;
        std::filesystem::path relativePath =
            std::filesystem::relative(entry.path(), mRootPath, relativeError);
        if (relativeError || relativePath.empty() || Core::EscapesRoot(relativePath))
        {
            continue;
        }
        paths.push_back(relativePath.lexically_normal());
    }

    // The directory iterator gives no order, and the asset database numbers what it walks, so the
    // order is fixed here rather than left to the filesystem.
    std::ranges::sort(paths);
    return paths;
}

}
