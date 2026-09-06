#include "pch.h"
#include "ProjectBuilder.h"

#include "ContentPackWriter.h"
#include "../Platform/DirectoryContentSource.h"

#include "../App/ProjectFile.h"
#include "../Assets/AssetDatabase.h"
#include "../Assets/AssetIdentityIssue.h"
#include "../Core/Guid.h"
#include "../Core/RelativePath.h"
#include "../Rendering/GraphicsBackend.h"
#include "../Diagnostics/Debug.h"

#include <cctype>
#include <cstddef>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace GameEngine::Build
{

namespace
{

    std::wstring MakePathKey(const std::filesystem::path& path)
    {
        std::wstring key = path.lexically_normal().native();
        std::ranges::transform(
            key,
            key.begin(),
            [](const wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
        return key;
    }

    bool IsSameOrDescendant(
        const std::filesystem::path& candidate,
        const std::filesystem::path& directory)
    {
        const std::wstring candidateKey = MakePathKey(candidate);
        std::wstring directoryKey = MakePathKey(directory);
        if (candidateKey == directoryKey)
        {
            return true;
        }
        if (!directoryKey.ends_with(std::filesystem::path::preferred_separator))
        {
            directoryKey.push_back(std::filesystem::path::preferred_separator);
        }
        return candidateKey.starts_with(directoryKey);
    }

    std::optional<std::filesystem::path> ResolveExistingPath(
        const std::filesystem::path& path,
        const bool requireDirectory)
    {
        std::error_code error;
        const std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
        if (error ||
            (requireDirectory && !std::filesystem::is_directory(resolved, error)) ||
            (!requireDirectory && !std::filesystem::is_regular_file(resolved, error)) ||
            error)
        {
            return std::nullopt;
        }
        return resolved;
    }

    std::optional<std::filesystem::path> ResolveOutputPath(
        const std::filesystem::path& path)
    {
        if (path.empty())
        {
            return std::nullopt;
        }

        std::error_code error;
        std::filesystem::path absolutePath = std::filesystem::absolute(path, error).lexically_normal();
        if (error || absolutePath == absolutePath.root_path() || absolutePath.filename().empty())
        {
            return std::nullopt;
        }

        std::filesystem::create_directories(absolutePath.parent_path(), error);
        if (error)
        {
            return std::nullopt;
        }
        std::filesystem::path parentPath =
            std::filesystem::weakly_canonical(absolutePath.parent_path(), error);
        if (error)
        {
            return std::nullopt;
        }
        return (parentPath / absolutePath.filename()).lexically_normal();
    }

    bool CopyFileTo(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& destinationPath)
    {
        std::error_code error;
        std::filesystem::create_directories(destinationPath.parent_path(), error);
        if (error)
        {
            Diagnostics::Debug::LogError(
                "Failed to create build output directory. path=", destinationPath.string(),
                ", error=", error.message());
            return false;
        }

        std::filesystem::copy_file(
            sourcePath,
            destinationPath,
            std::filesystem::copy_options::overwrite_existing,
            error);
        if (error)
        {
            Diagnostics::Debug::LogError(
                "Failed to copy build file. source=", sourcePath.string(),
                ", destination=", destinationPath.string(),
                ", error=", error.message());
            return false;
        }
        return true;
    }

    bool RemoveDirectoryTree(const std::filesystem::path& path)
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (error)
        {
            Diagnostics::Debug::LogError(
                "Failed to remove build directory. path=", path.string(),
                ", error=", error.message());
            return false;
        }
        return true;
    }

    // Only directories this invocation created are disposable. In particular, output.staging
    // and output.backup may be somebody's source tree, so neither is a reserved scratch name.
    class BuildWorkspace final
    {
    public:
        ~BuildWorkspace()
        {
            if (!mPath.empty() && !mPreserve)
            {
                RemoveDirectoryTree(mPath);
            }
        }

        [[nodiscard]] bool Create(const std::filesystem::path& outputPath)
        {
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                const std::filesystem::path candidate = outputPath.parent_path() /
                    (outputPath.filename().wstring() + L".build-" +
                        std::filesystem::path(Core::MakeGuid().ToString()).wstring());
                std::error_code error;
                if (std::filesystem::create_directory(candidate, error))
                {
                    mPath = candidate;
                    return true;
                }
                if (error)
                {
                    Diagnostics::Debug::LogError(
                        "Failed to create an isolated build workspace. path=", candidate.string(),
                        ", error=", error.message());
                    return false;
                }
                // An existing directory was not created by this build. Pick another name.
            }
            Diagnostics::Debug::LogError("Could not reserve an isolated build workspace.");
            return false;
        }

        [[nodiscard]] std::filesystem::path StagingPath() const { return mPath / "staging"; }
        [[nodiscard]] std::filesystem::path BackupPath() const { return mPath / "previous"; }
        void Preserve() { mPreserve = true; }

    private:
        std::filesystem::path mPath;
        bool mPreserve = false;
    };

    bool CommitStagingDirectory(
        const std::filesystem::path& stagingPath,
        const std::filesystem::path& outputPath,
        const std::filesystem::path& backupPath,
        bool& preserveBackup)
    {
        std::error_code error;
        const bool hadPreviousOutput = std::filesystem::exists(outputPath, error);
        if (error)
        {
            return false;
        }
        if (hadPreviousOutput)
        {
            std::filesystem::rename(outputPath, backupPath, error);
            if (error)
            {
                Diagnostics::Debug::LogError(
                    "Failed to preserve the previous build output. error=", error.message());
                return false;
            }
        }

        std::filesystem::rename(stagingPath, outputPath, error);
        if (error)
        {
            Diagnostics::Debug::LogError(
                "Failed to publish the staged build output. error=", error.message());
            if (hadPreviousOutput)
            {
                std::error_code restoreError;
                std::filesystem::rename(backupPath, outputPath, restoreError);
                if (restoreError)
                {
                    preserveBackup = true;
                    Diagnostics::Debug::LogError(
                        "Failed to restore the previous build output; its backup is kept. path=",
                        backupPath.string(), ", error=", restoreError.message());
                }
            }
            return false;
        }

        if (hadPreviousOutput)
        {
            std::filesystem::remove_all(backupPath, error);
            if (error)
            {
                preserveBackup = true;
                Diagnostics::Debug::LogWarning(
                    "The new build was published, but its backup could not be removed. path=",
                    backupPath.string(), ", error=", error.message());
            }
        }
        return true;
    }

    /// <summary>
    /// 정체성이 없는 에셋을 어떻게 할지 정한다: 허락받았으면 발급하고, 아니면 거절한다.
    ///
    /// 거절할 때 몇 개인지와 <b>사람이 무엇을 할 수 있는지</b>를 함께 말한다. 「정체성이 없다」만
    /// 적으면 그것을 받은 사람은 다음으로 무엇을 할지 모른다.
    ///
    /// 발급할 때도 조용히 하지 않는다. 발급은 사용자의 프로젝트에 파일을 만들고, 그것을
    /// 플래그로 허락한 뜻은 「만들어도 좋다」이지 「만든 줄 몰라도 좋다」가 아니다.
    /// </summary>
    [[nodiscard]] bool IssueIdentitiesIfAllowed(
        Assets::AssetDatabase& assetDatabase,
        const Platform::DirectoryContentSource& projectContent,
        const bool allowed)
    {
        const auto missing = static_cast<std::size_t>(std::ranges::count_if(
            assetDatabase.GetAssets(),
            [](const std::unique_ptr<Assets::Asset>& asset)
            {
                return asset && !asset->GetGuid().IsValid();
            }));
        if (missing == 0)
        {
            return true;
        }

        if (!allowed)
        {
            Diagnostics::Debug::LogError(
                "This project has ", missing,
                " assets with no identity yet, and a package cannot refer to them. Open the "
                "project in the editor once, which gives every asset an identity, or pass "
                "--issue-identities to have this build create them.");
            return false;
        }

        const std::size_t issued = Assets::IssueMissingIdentities(assetDatabase, projectContent);
        Diagnostics::Debug::Log(
            "Gave an identity to ", issued,
            " assets that had none, and wrote it beside each of them in the project.");

        // 방금 쓴 사이드카는 디스크에만 있다. 다시 읽어야 매니페스트가 그것을 본다.
        return issued > 0 && assetDatabase.Refresh(projectContent);
    }

    std::optional<std::filesystem::path> ResolveContentFile(
        const std::filesystem::path& contentRootPath,
        const std::filesystem::path& relativePath)
    {
        if (relativePath.empty() || relativePath.is_absolute() ||
            Core::EscapesRoot(relativePath))
        {
            return std::nullopt;
        }

        const std::optional<std::filesystem::path> sourcePath = ResolveExistingPath(
            contentRootPath / relativePath,
            false);
        if (!sourcePath || !IsSameOrDescendant(*sourcePath, contentRootPath))
        {
            return std::nullopt;
        }
        return sourcePath;
    }

    /// <summary>
    /// 패키징된 프로젝트가 자기가 요청한 백엔드를 위해 지녀야 하는 파일들이다. 백엔드마다 자기
    /// 아티팩트를 선언하므로 빌드 시스템은 그래픽 API를 결코 열거하지 않는다. 자동 선택 요청은
    /// 대상 머신에서 결정되므로, 컴파일된 모든 백엔드의 아티팩트 합집합을 스테이징한다.
    /// </summary>
    std::optional<std::vector<std::filesystem::path>> GetRuntimeArtifactPaths(
        const std::string& graphicsApi)
    {
        if (!Rendering::GraphicsBackendRegistry::IsKnownId(graphicsApi))
        {
            Diagnostics::Debug::LogError(
                "The project requests a graphics backend this build does not provide. requested=",
                graphicsApi, ", available=",
                Rendering::GraphicsBackendRegistry::DescribeAvailableIds());
            return std::nullopt;
        }

        std::vector<std::filesystem::path> artifacts;
        const auto append = [&artifacts](const Rendering::GraphicsBackendDescriptor& backend)
        {
            for (std::filesystem::path& artifact : backend.GetRuntimeArtifacts())
            {
                if (std::ranges::find(artifacts, artifact) == artifacts.end())
                {
                    artifacts.push_back(std::move(artifact));
                }
            }
        };

        if (const Rendering::GraphicsBackendDescriptor* const backend =
                Rendering::GraphicsBackendRegistry::Find(graphicsApi))
        {
            append(*backend);
        }
        else
        {
            for (const Rendering::GraphicsBackendDescriptor& registered :
                 Rendering::GraphicsBackendRegistry::GetBackends())
            {
                append(registered);
            }
        }
        return artifacts;
    }
}

std::optional<ProjectBuildResult> ProjectBuilder::Build(const ProjectBuildRequest& request)
{
    const std::optional<std::filesystem::path> contentRootPath =
        ResolveExistingPath(request.contentRootPath, true);
    const std::optional<std::filesystem::path> executablePath =
        ResolveExistingPath(request.executablePath, false);
    const std::optional<std::filesystem::path> runtimeRootPath =
        ResolveExistingPath(request.runtimeRootPath, true);
    const std::optional<std::filesystem::path> outputPath =
        ResolveOutputPath(request.outputPath);
    if (!contentRootPath || !executablePath || !runtimeRootPath || !outputPath)
    {
        Diagnostics::Debug::LogError("Project build request contains a missing or invalid path.");
        return std::nullopt;
    }
    if (IsSameOrDescendant(*outputPath, *contentRootPath) ||
        IsSameOrDescendant(*contentRootPath, *outputPath) ||
        IsSameOrDescendant(*executablePath, *outputPath) ||
        IsSameOrDescendant(*outputPath, *runtimeRootPath) ||
        IsSameOrDescendant(*runtimeRootPath, *outputPath))
    {
        Diagnostics::Debug::LogError(
            "Build output must not overlap project content or compiled inputs. output=",
            outputPath->string());
        return std::nullopt;
    }

    const Platform::DirectoryContentSource projectContent(*contentRootPath);
    Assets::AssetDatabase assetDatabase;
    if (!assetDatabase.Refresh(projectContent))
    {
        return std::nullopt;
    }

    // 정체성이 없는 에셋이 있으면 매니페스트가 거부한다. 여기서 먼저 답하는 이유는 두 가지다:
    // 그 실패가 스테이징을 한참 지난 뒤에 나면 사람이 무엇 때문인지 찾아야 하고, 무엇보다
    // 사람이 할 수 있는 일을 여기서만 말해 줄 수 있기 때문이다 — 매니페스트는 이 도구에 어떤
    // 플래그가 있는지 모른다.
    if (!IssueIdentitiesIfAllowed(assetDatabase, projectContent, request.issueMissingIdentities))
    {
        return std::nullopt;
    }

    const std::optional<std::filesystem::path> sourceProjectFilePath =
        App::ProjectFile::FindInDirectory(*contentRootPath);
    const std::optional<App::ProjectFileData> projectFile = sourceProjectFilePath
        ? App::ProjectFile::Load(*sourceProjectFilePath)
        : std::nullopt;
    if (!projectFile)
    {
        return std::nullopt;
    }
    // 에셋 루트는 언제나 .gameproject가 있는 자리이므로 패키지는 그 루트를 기준으로 배치한다.
    const App::ProjectSettings& projectSettings = projectFile->settings;

    const std::optional<std::vector<std::filesystem::path>> runtimeArtifactPaths =
        GetRuntimeArtifactPaths(projectSettings.graphicsApi);
    if (!runtimeArtifactPaths)
    {
        return std::nullopt;
    }

    BuildWorkspace workspace;
    if (!workspace.Create(*outputPath))
    {
        return std::nullopt;
    }
    const std::filesystem::path stagingPath = workspace.StagingPath();

    std::error_code error;
    std::filesystem::create_directories(stagingPath, error);
    if (error)
    {
        Diagnostics::Debug::LogError(
            "Failed to create the build staging directory. error=", error.message());
        return std::nullopt;
    }

    const auto failBuild = []() -> std::optional<ProjectBuildResult>
    {
        return std::nullopt;
    };

    for (const std::unique_ptr<Assets::Asset>& asset : assetDatabase.GetAssets())
    {
        if (!CopyFileTo(asset->GetSourcePath(), stagingPath / asset->GetRelativePath()))
        {
            return failBuild();
        }
    }

    std::size_t sceneCount = 0;
    for (const auto& [sceneId, scenePath] : projectSettings.scenePaths)
    {
        const Assets::SceneAsset* sceneAsset = assetDatabase.FindAsset<Assets::SceneAsset>(scenePath);
        if (!sceneAsset)
        {
            Diagnostics::Debug::LogError(
                "Project scene is missing or is not a registered Scene asset. sceneId=", sceneId,
                ", path=", scenePath.string());
            return failBuild();
        }
        ++sceneCount;
    }

    if (!CopyFileTo(*executablePath, stagingPath / executablePath->filename()))
    {
        return failBuild();
    }
    std::filesystem::path symbolPath = *executablePath;
    symbolPath.replace_extension(".pdb");
    if (std::filesystem::is_regular_file(symbolPath, error) && !error &&
        !CopyFileTo(symbolPath, stagingPath / symbolPath.filename()))
    {
        return failBuild();
    }

    std::filesystem::directory_iterator binaryIterator(executablePath->parent_path(), error);
    const std::filesystem::directory_iterator end;
    while (!error && binaryIterator != end)
    {
        const std::filesystem::directory_entry entry = *binaryIterator;
        binaryIterator.increment(error);
        std::wstring extension = entry.path().extension().native();
        std::ranges::transform(extension, extension.begin(), [](const wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        if (entry.is_regular_file() && extension == L".dll" &&
            !CopyFileTo(entry.path(), stagingPath / entry.path().filename()))
        {
            return failBuild();
        }
    }
    if (error)
    {
        Diagnostics::Debug::LogError(
            "Failed while scanning compiled runtime dependencies. error=", error.message());
        return failBuild();
    }

    for (const std::filesystem::path& runtimeArtifactPath : *runtimeArtifactPaths)
    {
        const std::optional<std::filesystem::path> sourceRuntimePath =
            ResolveContentFile(*runtimeRootPath, runtimeArtifactPath);
        if (!sourceRuntimePath ||
            !CopyFileTo(*sourceRuntimePath, stagingPath / runtimeArtifactPath))
        {
            Diagnostics::Debug::LogError(
                "Required engine runtime artifact is missing. path=",
                runtimeArtifactPath.string());
            return failBuild();
        }
    }

    // 스테이징된 셰이더 소스를 사전 컴파일해, 배포된 게임이 실행 시 컴파일 없이 셰이더를
    // 로드하게 한다. 어떻게 컴파일하는지는 백엔드 계열의 지식이므로 서술자가 한다. 실패는
    // 배포를 막지 않는다: 런타임이 소스에서 컴파일하는 예비 경로를 그대로 갖고 있으므로, 이
    // 단계는 정확성이 아니라 시작 비용의 문제다.
    {
        const auto precompile = [&stagingPath](const Rendering::GraphicsBackendDescriptor& backend)
        {
            if (backend.CompileRuntimeArtifacts && !backend.CompileRuntimeArtifacts(stagingPath))
            {
                Diagnostics::Debug::LogWarning(
                    "Shader precompilation did not complete; the game will compile from source. "
                    "backend=", backend.id);
            }
        };
        if (const Rendering::GraphicsBackendDescriptor* const backend =
                Rendering::GraphicsBackendRegistry::Find(projectSettings.graphicsApi))
        {
            precompile(*backend);
        }
        else
        {
            for (const Rendering::GraphicsBackendDescriptor& registered :
                 Rendering::GraphicsBackendRegistry::GetBackends())
            {
                precompile(registered);
            }
        }
    }

    const std::filesystem::path manifestPath =
        stagingPath / Assets::AssetDatabase::ManifestRelativePath;
    if (!assetDatabase.SaveManifest(manifestPath))
    {
        return failBuild();
    }

    // Validated through a source over the staging directory, the same way the game will read it.
    const Platform::DirectoryContentSource stagedContentForValidation(stagingPath);
    Assets::AssetDatabase stagedDatabase;
    const std::optional<std::filesystem::path> stagedProjectFilePath =
        App::ProjectFile::FindInSource(stagedContentForValidation);
    if (!stagedDatabase.LoadManifest(
            stagedContentForValidation, Assets::AssetDatabase::ManifestRelativePath) ||
        !stagedProjectFilePath ||
        !App::ProjectFile::Load(stagedContentForValidation, *stagedProjectFilePath))
    {
        Diagnostics::Debug::LogError("Staged project package validation failed.");
        return failBuild();
    }

    // The package is assembled and validated as a directory whichever kind of output was asked for.
    // Folding it into the executable is the last step, so a single-file build is checked exactly as
    // thoroughly as one that ships a folder.
    bool packedIntoExecutable = false;
    if (request.packContentIntoExecutable)
    {
        const std::filesystem::path stagedExecutablePath = stagingPath / executablePath->filename();
        const Platform::DirectoryContentSource stagedContent(stagingPath);
        std::vector<std::filesystem::path> packedPaths;
        for (const std::filesystem::path& relativePath : stagedContent.List())
        {
            // Native DLLs must stay on disk: the OS loader needs them before the player can read
            // its content pack. Symbols likewise stay outside the game content.
            const std::wstring extension = MakePathKey(relativePath.extension());
            if (relativePath == stagedExecutablePath.filename() || extension == L".pdb" ||
                extension == L".dll")
            {
                continue;
            }
            packedPaths.push_back(relativePath);
        }

        if (!AppendContentPack(stagedExecutablePath, stagingPath, packedPaths))
        {
            return failBuild();
        }

        // Remove only content folded into the executable. Native dependencies remain beside it.
        for (const std::filesystem::path& relativePath : packedPaths)
        {
            std::error_code removeError;
            std::filesystem::remove(stagingPath / relativePath, removeError);
        }
        packedIntoExecutable = true;
    }

    bool preserveBackup = false;
    const bool committed = CommitStagingDirectory(
        stagingPath, *outputPath, workspace.BackupPath(), preserveBackup);
    if (preserveBackup)
    {
        workspace.Preserve();
    }
    if (!committed)
    {
        return std::nullopt;
    }

    Diagnostics::Debug::Log(
        "Project package built. output=", outputPath->string(),
        ", assets=", assetDatabase.GetAssets().size(),
        ", scenes=", sceneCount,
        ", runtimeFiles=", runtimeArtifactPaths->size(),
        ", packed=", packedIntoExecutable ? "yes" : "no");
    return ProjectBuildResult{
        *outputPath,
        assetDatabase.GetAssets().size(),
        sceneCount,
        runtimeArtifactPaths->size(),
        packedIntoExecutable
    };
}

}
