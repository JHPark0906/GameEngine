#include "pch.h"
#include "CMakeLocation.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <system_error>
#include <vector>

namespace GameEngine::Build
{

namespace
{
    /// <summary>
    /// 인자 하나를 Windows의 명령줄 파서가 되읽을 수 있게 감싼다. 여기서만 쓴다 — vswhere를
    /// 셸로 부르는 아래 한 자리다. 프로세스를 띄우는 쪽의 따옴표 규칙은 플랫폼 계층이 쥔다.
    /// </summary>
    std::wstring QuoteCommandLineArgument(const std::wstring_view argument)
    {
        std::wstring quoted;
        quoted.push_back(L'"');
        std::size_t backslashCount = 0;
        for (const wchar_t character : argument)
        {
            if (character == L'\\')
            {
                ++backslashCount;
                continue;
            }
            if (character == L'"')
            {
                quoted.append(backslashCount * 2 + 1, L'\\');
                quoted.push_back(character);
                backslashCount = 0;
                continue;
            }
            quoted.append(backslashCount, L'\\');
            backslashCount = 0;
            quoted.push_back(character);
        }
        quoted.append(backslashCount * 2, L'\\');
        quoted.push_back(L'"');
        return quoted;
    }
}

std::optional<std::filesystem::path> FindCMakeOnPath()
{
    const DWORD requiredLength = SearchPathW(nullptr, L"cmake.exe", nullptr, 0, nullptr, nullptr);
    if (requiredLength == 0)
    {
        return std::nullopt;
    }

    std::vector<wchar_t> buffer(static_cast<std::size_t>(requiredLength) + 1);
    if (SearchPathW(
            nullptr,
            L"cmake.exe",
            nullptr,
            static_cast<DWORD>(buffer.size()),
            buffer.data(),
            nullptr) == 0)
    {
        return std::nullopt;
    }
    return std::filesystem::path(buffer.data());
}

std::optional<std::filesystem::path> FindCMakeWithVsWhere()
{
    wchar_t* programFilesPath = nullptr;
    std::size_t characterCount = 0;
    if (_wdupenv_s(&programFilesPath, &characterCount, L"ProgramFiles(x86)") != 0 ||
        !programFilesPath)
    {
        return std::nullopt;
    }
    const std::filesystem::path vsWherePath =
        std::filesystem::path(programFilesPath) /
        L"Microsoft Visual Studio/Installer/vswhere.exe";
    std::free(programFilesPath);
    if (!std::filesystem::is_regular_file(vsWherePath))
    {
        return std::nullopt;
    }

    // Visual Studio ships a CMake of its own; asking vswhere for it means a machine with only
    // Visual Studio installed can still build a project from the command line.
    const std::wstring command = L"\"" + QuoteCommandLineArgument(vsWherePath.native()) +
        L" -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project" +
        L" -find \"Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe\"\"";
    FILE* pipe = _wpopen(command.c_str(), L"rt");
    if (!pipe)
    {
        return std::nullopt;
    }

    std::array<wchar_t, 32768> buffer{};
    std::wstring result;
    if (std::fgetws(buffer.data(), static_cast<int>(buffer.size()), pipe))
    {
        result = buffer.data();
    }
    const int exitCode = _pclose(pipe);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n'))
    {
        result.pop_back();
    }
    if (exitCode != 0 || result.empty() || !std::filesystem::is_regular_file(result))
    {
        return std::nullopt;
    }
    return std::filesystem::path(result);
}

/// <summary>
/// One entry of a configured build tree's cache, or nothing when the tree or the entry is
/// absent. Asking the tree is how this tool learns what the build decided instead of deciding
/// it a second time and drifting.
/// </summary>
/// <param name="buildDirectory">Directory holding CMakeCache.txt.</param>
/// <param name="key">The entry with its type, as the cache writes it: "NAME:TYPE=".</param>
std::optional<std::string> ReadCacheEntry(
    const std::filesystem::path& buildDirectory, const std::string_view key)
{
    std::ifstream cache(buildDirectory / L"CMakeCache.txt");
    std::string line;
    while (std::getline(cache, line))
    {
        if (!line.starts_with(key))
        {
            continue;
        }
        std::string value = line.substr(key.size());
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
        {
            value.pop_back();
        }
        if (value.empty())
        {
            return std::nullopt;
        }
        return value;
    }
    return std::nullopt;
}

/// <summary>
/// The cmake to build this tree with.
///
/// An explicit --cmake wins. Otherwise use the cmake recorded in CMakeCache.txt, then the
/// version shipped with Visual Studio, and finally PATH. The cached executable supports the
/// tree's generator; an unrelated cmake on PATH may not support it.
/// </summary>
/// <param name="requestedPath">--cmake, or empty.</param>
/// <param name="buildDirectory">The configured build tree, whose cache is asked first.</param>
std::optional<std::filesystem::path> ResolveCMakePath(
    const std::filesystem::path& requestedPath,
    const std::filesystem::path& buildDirectory)
{
    if (!requestedPath.empty())
    {
        std::error_code error;
        const std::filesystem::path resolved =
            std::filesystem::weakly_canonical(requestedPath, error);
        if (!error && std::filesystem::is_regular_file(resolved, error) && !error)
        {
            return resolved;
        }
        return std::nullopt;
    }
    if (const std::optional<std::string> recorded =
            ReadCacheEntry(buildDirectory, "CMAKE_COMMAND:INTERNAL="))
    {
        const std::filesystem::path cached(std::u8string(recorded->begin(), recorded->end()));
        std::error_code error;
        if (std::filesystem::is_regular_file(cached, error) && !error)
        {
            return cached;
        }
    }
    if (const auto vsResult = FindCMakeWithVsWhere())
    {
        return vsResult;
    }
    return FindCMakeOnPath();
}


}
