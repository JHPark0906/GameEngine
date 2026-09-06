#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "App/ProjectFile.h"
#include "Build/CMakeLocation.h"
#include "Build/ProjectBuildSource.h"
#include "Build/ProjectBuilder.h"
#include "Platform/ProcessRun.h"

namespace
{
    struct Options
    {
        std::filesystem::path projectPath;
        std::filesystem::path outputPath;
        std::filesystem::path buildDirectory;
        std::filesystem::path cmakePath;
        std::wstring configuration = L"Debug";
        std::wstring targetName;
        bool singleFile = false;
        bool issueIdentities = false;
    };

    void PrintUsage()
    {
        std::wcout
            << L"Usage: GameBuilder --project <project directory> --output <directory>\n"
            << L"  [--configuration Debug|Release] [--build-dir <configured CMake build tree>]\n"
            << L"  [--target-name <CMake target and executable name>] [--cmake <cmake.exe>]\n"
            << L"  [--single-file]  pack content into the executable; native DLLs stay beside it\n"
            << L"  [--issue-identities]  give an identity to any asset that has none, writing it\n"
            << L"                        beside that asset in the project. Off by default: a build\n"
            << L"                        should not create files in your project unless you say so.\n"
            << L"\n"
            << L"The project directory holds the project's CMakeLists.txt and its Content directory.\n"
            << L"The build tree defaults to <repository>/build/vs and must already be configured\n"
            << L"(cmake --preset vs); the tool builds the target there and packages what it produced.\n";
    }

    bool IsSimpleCommandLineValue(const std::wstring_view value)
    {
        return !value.empty() && std::ranges::all_of(value, [](const wchar_t character)
        {
            return std::iswalnum(character) || character == L'_' || character == L'-';
        });
    }

    std::optional<Options> ParseOptions(const int argumentCount, wchar_t* arguments[])
    {
        Options options;
        for (int index = 1; index < argumentCount; ++index)
        {
            const std::wstring_view argument = arguments[index];
            if (argument == L"--help" || argument == L"-h")
            {
                PrintUsage();
                return std::nullopt;
            }
            if (argument == L"--single-file")
            {
                options.singleFile = true;
                continue;
            }
            if (argument == L"--issue-identities")
            {
                options.issueIdentities = true;
                continue;
            }
            if (index + 1 >= argumentCount)
            {
                std::wcerr << L"Missing value for " << argument << L".\n";
                return std::nullopt;
            }

            const std::wstring value = arguments[++index];
            if (argument == L"--project")
            {
                options.projectPath = value;
            }
            else if (argument == L"--output")
            {
                options.outputPath = value;
            }
            else if (argument == L"--configuration")
            {
                options.configuration = value;
            }
            else if (argument == L"--build-dir")
            {
                options.buildDirectory = value;
            }
            else if (argument == L"--target-name")
            {
                options.targetName = value;
            }
            else if (argument == L"--cmake")
            {
                options.cmakePath = value;
            }
            else
            {
                std::wcerr << L"Unknown option: " << argument << L".\n";
                return std::nullopt;
            }
        }

        if (options.projectPath.empty() || options.outputPath.empty() ||
            !IsSimpleCommandLineValue(options.configuration) ||
            (!options.targetName.empty() && !IsSimpleCommandLineValue(options.targetName)))
        {
            PrintUsage();
            return std::nullopt;
        }
        return options;
    }

    /// <summary>
    /// Where the configured build tree puts its products. The tree knows: GAMEENGINE_OUTPUT_ROOT is
    /// a cache entry the repository's CMakeLists defines, so reading it here is asking the build
    /// rather than guessing the layout twice.
    /// </summary>
    std::optional<std::filesystem::path> ReadOutputRoot(const std::filesystem::path& buildDirectory)
    {
        const std::optional<std::string> value =
            GameEngine::Build::ReadCacheEntry(buildDirectory, "GAMEENGINE_OUTPUT_ROOT:PATH=");
        if (!value)
        {
            return std::nullopt;
        }
        return std::filesystem::path(std::u8string(value->begin(), value->end()));
    }

    bool IsSameOrDescendant(
        const std::filesystem::path& candidate,
        const std::filesystem::path& directory)
    {
        std::wstring candidateKey = candidate.lexically_normal().native();
        std::wstring directoryKey = directory.lexically_normal().native();
        std::ranges::transform(candidateKey, candidateKey.begin(), [](const wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        std::ranges::transform(directoryKey, directoryKey.begin(), [](const wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
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

    /// <summary>The output directory, made absolute and checked to lie outside the project.</summary>
    std::optional<std::filesystem::path> ResolveOutputDirectory(
        const std::filesystem::path& requestedOutputPath,
        const std::filesystem::path& projectDirectory)
    {
        std::error_code error;
        std::filesystem::path outputPath =
            std::filesystem::absolute(requestedOutputPath, error).lexically_normal();
        if (error || outputPath == outputPath.root_path() || outputPath.filename().empty())
        {
            return std::nullopt;
        }
        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error)
        {
            return std::nullopt;
        }
        outputPath = std::filesystem::weakly_canonical(outputPath.parent_path(), error) /
            outputPath.filename();
        if (error || IsSameOrDescendant(outputPath, projectDirectory))
        {
            std::wcerr << L"Output must be outside the project source directory.\n";
            return std::nullopt;
        }
        return outputPath;
    }

    /// <summary>
    /// 와이드 문자열을 UTF-8로 바꾼다. 경로 타입을 다리로 쓰므로 어느 플랫폼의 변환 API에도
    /// 기대지 않는다. 프로세스 설비가 받는 인자가 UTF-8이라 이 다리가 필요하다.
    /// </summary>
    [[nodiscard]] std::string ToUtf8(const std::wstring& text)
    {
        const std::u8string utf8 = std::filesystem::path(text).u8string();
        return std::string(utf8.begin(), utf8.end());
    }

    /// <summary>
    /// 프로그램 하나를 끝날 때까지 돌리고 그 출력을 이 콘솔에 흘린다.
    ///
    /// 프로세스를 띄우는 일은 엔진의 설비가 한다. 이 도구가 자기 것을 따로 쓰면 자식이 낳는
    /// 손자를 어떻게 끝낼지, 콘솔이 쓴 글자를 어느 코드페이지로 읽을지를 두 곳에서 각각 정하게
    /// 되고, 그렇게 갈라진 두 규칙 중 하나는 반드시 틀린 채로 남는다.
    /// </summary>
    /// <returns>종료 코드다. 시작하지 못했으면 실패 코드다.</returns>
    int RunProcess(
        const std::filesystem::path& executablePath,
        const std::vector<std::string>& arguments,
        const std::filesystem::path& workingDirectory)
    {
        GameEngine::Platform::ProcessRequest request;
        request.executable = executablePath;
        request.arguments = arguments;
        request.workingDirectory = workingDirectory;

        const std::optional<int> exitCode = GameEngine::Platform::RunProcessToCompletion(
            request, [](const std::string_view line)
            {
                // 넘어오는 줄은 이미 UTF-8이다. 바이트 그대로 쓴다 — 와이드 스트림에 넣으면 이
                // 로캘의 코드페이지로 한 번 더 해석되어 글자가 깨진다.
                std::cout.write(line.data(), static_cast<std::streamsize>(line.size()));
                std::cout << "\n" << std::flush;
            });
        if (!exitCode)
        {
            std::wcerr << L"Failed to launch " << executablePath.filename().native() << L".\n";
            return EXIT_FAILURE;
        }
        return *exitCode;
    }
}

int wmain(const int argumentCount, wchar_t* arguments[])
{
    const std::optional<Options> options = ParseOptions(argumentCount, arguments);
    if (!options)
    {
        return argumentCount > 1 &&
            (std::wstring_view(arguments[1]) == L"--help" ||
             std::wstring_view(arguments[1]) == L"-h")
            ? EXIT_SUCCESS
            : EXIT_FAILURE;
    }

    std::error_code error;
    const std::filesystem::path projectDirectory =
        std::filesystem::weakly_canonical(options->projectPath, error);
    if (error || !std::filesystem::is_directory(projectDirectory, error) || error ||
        !std::filesystem::is_regular_file(projectDirectory / L"CMakeLists.txt", error))
    {
        std::wcerr << L"Project directory is missing or has no CMakeLists.txt.\n";
        return EXIT_FAILURE;
    }
    // 콘텐츠 루트는 프로젝트가 정하며, .gameproject가 있는 자리로 판정한다.
    // Content/ 아래와 프로젝트 루트 배치 모두 이 규칙을 따른다.
    const std::filesystem::path contentRootPath =
        GameEngine::App::ProjectFile::FindContentRoot(projectDirectory);
    if (contentRootPath.empty())
    {
        std::wcerr << L"No .gameproject in " << (projectDirectory / L"Content").native()
                   << L" or " << projectDirectory.native()
                   << L"\nA project keeps its content in Content/ or beside its .gameproject at "
                      L"the project root; this one has neither.\n";
        return EXIT_FAILURE;
    }
    const std::wstring targetName = options->targetName.empty()
        ? projectDirectory.filename().native()
        : options->targetName;
    if (!IsSimpleCommandLineValue(targetName))
    {
        std::wcerr << L"The project directory name is not usable as a target name; pass --target-name.\n";
        return EXIT_FAILURE;
    }

    // The build tree is the repository's default preset unless told otherwise. Configuring is left
    // to the developer on purpose: which generator and which options a tree was made with is a
    // decision this tool has no business re-making on every build.
    const std::filesystem::path buildDirectory = std::filesystem::weakly_canonical(
        options->buildDirectory.empty()
            ? projectDirectory.parent_path() / L"build" / L"vs"
            : options->buildDirectory,
        error);
    if (error || !std::filesystem::is_regular_file(buildDirectory / L"CMakeCache.txt", error))
    {
        std::wcerr << L"The build tree is not configured: " << buildDirectory.native()
                   << L"\nRun `cmake --preset vs` in the repository first, or pass --build-dir.\n";
        return EXIT_FAILURE;
    }
    std::optional<std::filesystem::path> outputRoot = ReadOutputRoot(buildDirectory);
    if (!outputRoot)
    {
        std::wcerr << L"The build tree does not define GAMEENGINE_OUTPUT_ROOT; is it this repository's?\n";
        return EXIT_FAILURE;
    }

    std::string sourceError;
    if (!GameEngine::Build::ValidateProjectBuildSource(
            buildDirectory, ToUtf8(targetName), projectDirectory, sourceError))
    {
        std::cerr << sourceError << '\n';
        return EXIT_FAILURE;
    }

    const std::optional<std::filesystem::path> cmakePath =
        GameEngine::Build::ResolveCMakePath(options->cmakePath, buildDirectory);
    if (!cmakePath)
    {
        std::wcerr << L"cmake.exe was not found. Pass --cmake explicitly.\n";
        return EXIT_FAILURE;
    }
    const std::optional<std::filesystem::path> outputPath =
        ResolveOutputDirectory(options->outputPath, projectDirectory);
    if (!outputPath)
    {
        std::wcerr << L"Could not resolve the output directory.\n";
        return EXIT_FAILURE;
    }

    const std::vector<std::string> cmakeArguments{
        "--build", ToUtf8(buildDirectory.native()),
        "--config", ToUtf8(options->configuration),
        "--target", ToUtf8(targetName),
        "--parallel"
    };
    std::wcout << L"Compiling " << targetName << L" (" << options->configuration << L")...\n" << std::flush;
    const int buildExitCode = RunProcess(*cmakePath, cmakeArguments, buildDirectory);
    if (buildExitCode != EXIT_SUCCESS)
    {
        std::wcerr << L"CMake build failed with exit code " << buildExitCode << L".\n";
        return buildExitCode;
    }

    // CMake can reconfigure while building. Validate its resulting target source as well,
    // so a changed project selection cannot publish a different project's executable.
    if (!GameEngine::Build::ValidateProjectBuildSource(
            buildDirectory, ToUtf8(targetName), projectDirectory, sourceError))
    {
        std::cerr << sourceError << '\n';
        return EXIT_FAILURE;
    }
    outputRoot = ReadOutputRoot(buildDirectory);
    if (!outputRoot)
    {
        std::wcerr << L"The completed build no longer defines GAMEENGINE_OUTPUT_ROOT.\n";
        return EXIT_FAILURE;
    }

    const std::filesystem::path compiledPath = *outputRoot / options->configuration / targetName;
    const std::filesystem::path executablePath = compiledPath / (targetName + L".exe");
    if (!std::filesystem::is_regular_file(executablePath, error) || error)
    {
        std::wcerr << L"The build produced no executable at " << executablePath.native() << L".\n";
        return EXIT_FAILURE;
    }
    const GameEngine::Build::ProjectBuildRequest request{
        contentRootPath,
        executablePath,
        compiledPath,
        *outputPath,
        options->singleFile,
        options->issueIdentities
    };
    const std::optional<GameEngine::Build::ProjectBuildResult> result =
        GameEngine::Build::ProjectBuilder::Build(request);
    if (!result)
    {
        std::wcerr << L"Project packaging failed.\n";
        return EXIT_FAILURE;
    }

    std::wcout
        << L"Build completed: " << result->outputPath.native() << L"\n"
        << L"Assets: " << result->assetCount
        << L", scenes: " << result->sceneCount
        << L", runtime files: " << result->runtimeFileCount << L"\n";
    if (result->packedIntoExecutable)
    {
        std::wcout << L"Content is inside the executable; any native DLL dependencies remain beside it.\n";
    }
    return EXIT_SUCCESS;
}
