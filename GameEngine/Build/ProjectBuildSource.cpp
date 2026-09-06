#include "pch.h"
#include "ProjectBuildSource.h"

#include <optional>
#include <system_error>

#include "CMakeLocation.h"

namespace GameEngine::Build
{

bool ValidateProjectBuildSource(
    const std::filesystem::path& buildDirectory,
    const std::string_view targetName,
    const std::filesystem::path& projectDirectory,
    std::string& errorMessage)
{
    errorMessage.clear();
    if (targetName.empty() || targetName.find_first_of(":=\r\n") != std::string_view::npos)
    {
        errorMessage = "The game target name cannot identify a source entry in CMakeCache.txt.";
        return false;
    }

    const std::optional<std::string> recorded = ReadCacheEntry(
        buildDirectory, "GAMEENGINE_PROJECT_SOURCE_" + std::string(targetName) + ":INTERNAL=");
    if (!recorded)
    {
        errorMessage = "The build tree has no source metadata for game target '" +
            std::string(targetName) +
            "'. Configure it with this engine and the intended game project before packaging.";
        return false;
    }
    std::filesystem::path configuredSource;
    try
    {
        configuredSource = std::filesystem::path(std::u8string(recorded->begin(), recorded->end()));
    }
    catch (const std::system_error&)
    {
        errorMessage = "The game target's recorded source directory is not a valid UTF-8 path.";
        return false;
    }
    std::error_code error;
    if (recorded->find('\0') != std::string::npos || !configuredSource.is_absolute() ||
        !std::filesystem::is_directory(configuredSource, error) || error)
    {
        errorMessage = "The game target's recorded source directory is missing or invalid: " +
            *recorded + ". Configure the build tree again before packaging.";
        return false;
    }
    if (!std::filesystem::is_directory(projectDirectory, error) || error)
    {
        errorMessage = "The requested project source directory is unavailable.";
        return false;
    }
    configuredSource = std::filesystem::canonical(configuredSource, error);
    if (error)
    {
        errorMessage = "Could not resolve the game target's source directory: " + error.message();
        return false;
    }
    const std::filesystem::path requestedSource = std::filesystem::canonical(projectDirectory, error);
    if (error)
    {
        errorMessage = "Could not resolve the requested project source directory: " + error.message();
        return false;
    }
    // Canonical paths resolve case, dot segments, and junction/symlink aliases without requiring
    // file IDs, which some filesystems do not provide. Distinct names can still share an identity.
    if (configuredSource == requestedSource)
    {
        return true;
    }
    if (!std::filesystem::equivalent(configuredSource, requestedSource, error))
    {
        const std::u8string requestedUtf8 = requestedSource.generic_u8string();
        const std::string requested(requestedUtf8.begin(), requestedUtf8.end());
        errorMessage = "The game target '" + std::string(targetName) + "' compiles " +
            *recorded + ", while --project resolves to " + requested + ". ";
        errorMessage += error
            ? "Their canonical paths differ and file identity comparison failed: " + error.message() + ". "
            : "The requested project is a different directory. ";
        errorMessage += "Select the project's configured build tree or configure this tree for the "
            "requested project before packaging.";
        return false;
    }
    return true;
}

}
