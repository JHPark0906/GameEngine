#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace GameEngine::Build
{

/// <summary>
/// Checks that a configured game target compiles the requested project's source directory.
/// Missing metadata fails closed; the caller must configure the tree before packaging it.
/// </summary>
/// <param name="errorMessage">Empty on success, otherwise the reason packaging must stop.</param>
[[nodiscard]] bool ValidateProjectBuildSource(
    const std::filesystem::path& buildDirectory,
    std::string_view targetName,
    const std::filesystem::path& projectDirectory,
    std::string& errorMessage);

}
