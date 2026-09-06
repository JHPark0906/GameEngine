#include "pch.h"
#include "Win32FileMapping.h"

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>

#include "Win32Diagnostics.h"

namespace GameEngine::Platform::Win32
{

Win32FileMapping::Win32FileMapping(const std::filesystem::path& path)
{
    // Shared for reading and deleting, because this is usually the running executable and the
    // loader already holds it that way. Anything stricter fails on the one file that matters most.
    const HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        LogHResult("Opening a file to map", HRESULT_FROM_WIN32(GetLastError()));
        return;
    }

    LARGE_INTEGER size{};
    const bool haveSize = GetFileSizeEx(file, &size) != FALSE;
    if (!haveSize || size.QuadPart <= 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > (std::numeric_limits<std::size_t>::max)())
    {
        // An empty file has nothing to map, which the section API reports as an error. It is not one.
        if (haveSize && size.QuadPart == 0)
        {
            CloseHandle(file);
            return;
        }
        LogHResult("Sizing a file to map", HRESULT_FROM_WIN32(GetLastError()));
        CloseHandle(file);
        return;
    }

    const HANDLE section = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    // The view keeps the mapping alive on its own, so neither handle is needed past this point.
    CloseHandle(file);
    if (!section)
    {
        LogHResult("Creating a file mapping", HRESULT_FROM_WIN32(GetLastError()));
        return;
    }

    const void* const view = MapViewOfFile(section, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(section);
    if (!view)
    {
        LogHResult("Mapping a file into memory", HRESULT_FROM_WIN32(GetLastError()));
        return;
    }

    mBytes = { static_cast<const std::byte*>(view), static_cast<std::size_t>(size.QuadPart) };
}

Win32FileMapping::~Win32FileMapping()
{
    if (!mBytes.empty())
    {
        static_cast<void>(UnmapViewOfFile(mBytes.data()));
    }
}

}
