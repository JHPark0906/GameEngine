#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace GameEngine::Build
{

/// <summary>
/// One entry of a configured build tree's cache, or nothing when the tree or the entry is absent.
///
/// Asking the tree is how this tool learns what the build decided instead of deciding it a second
/// time and drifting.
/// </summary>
/// <param name="buildDirectory">Directory holding CMakeCache.txt.</param>
/// <param name="key">The entry with its type, as the cache writes it: "NAME:TYPE=".</param>
[[nodiscard]] std::optional<std::string> ReadCacheEntry(
    const std::filesystem::path& buildDirectory, std::string_view key);

/// <summary>The cmake Visual Studio ships, found through vswhere, or nothing.</summary>
[[nodiscard]] std::optional<std::filesystem::path> FindCMakeWithVsWhere();

/// <summary>The first cmake.exe on PATH, or nothing.</summary>
[[nodiscard]] std::optional<std::filesystem::path> FindCMakeOnPath();

/// <summary>
/// The cmake to build a tree with.
///
/// An explicit request wins. Otherwise use the cmake recorded in CMakeCache.txt, then the
/// version shipped with Visual Studio, and finally PATH. The cached executable supports the
/// tree's generator; an unrelated cmake on PATH may not support it.
/// </summary>
/// <param name="requestedPath">--cmake, or empty.</param>
/// <param name="buildDirectory">The configured build tree, whose cache is asked first.</param>
[[nodiscard]] std::optional<std::filesystem::path> ResolveCMakePath(
    const std::filesystem::path& requestedPath, const std::filesystem::path& buildDirectory);

}
