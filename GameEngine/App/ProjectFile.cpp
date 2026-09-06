#include "pch.h"
#include "../Platform/DirectoryContentSource.h"
#include "ProjectFile.h"

#include "ProjectSettingsLoader.h"
#include "../Platform/RelativePath.h"
#include "../Platform/TextFile.h"
#include "../Diagnostics/Debug.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace GameEngine::App
{

namespace
{
    std::wstring ToLower(std::wstring value)
    {
        std::ranges::transform(value, value.begin(), [](const wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        return value;
    }

    /// <summary>
    /// 경로 조각을 UTF-8로 바꾼다. 프로젝트 이름은 경로의 stem에서 오므로, 변환은 표준
    /// 라이브러리의 몫이고 이 파일이 플랫폼 텍스트 API를 부를 이유가 없다.
    /// </summary>
    std::string PathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string value = path.generic_u8string();
        return {
            reinterpret_cast<const char*>(value.data()),
            reinterpret_cast<const char*>(value.data() + value.size())
        };
    }

    std::string EscapeJsonString(const std::string_view value)
    {
        std::string result;
        constexpr char HexDigits[] = "0123456789abcdef";
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (character < 0x20)
                {
                    result += "\\u00";
                    result.push_back(HexDigits[character >> 4]);
                    result.push_back(HexDigits[character & 0x0f]);
                }
                else
                {
                    result.push_back(static_cast<char>(character));
                }
                break;
            }
        }
        return result;
    }

    /// <summary>
    /// 새 프로젝트와 장면을 만드는 자리들이 쓰는 짧은 이름이다.
    ///
    /// 이들은 두 파일을 함께 바꿔야 하므로 임시 파일을 자기가 다룬다 — 둘 중 하나만 놓이면
    /// 장면 없는 프로젝트가 남기 때문이다. 그래서 원자적 쓰기가 아니라 단순한 쓰기를 부른다.
    /// </summary>
    bool WriteTextFile(
        const std::filesystem::path& filePath,
        const std::string_view contents)
    {
        return static_cast<bool>(Platform::WriteTextFile(filePath, contents));
    }
}

std::optional<std::filesystem::path> ProjectFile::FindInDirectory(
    const std::filesystem::path& directoryPath)
{
    std::error_code error;
    const std::filesystem::path resolvedDirectory =
        std::filesystem::weakly_canonical(directoryPath, error);
    if (error || !std::filesystem::is_directory(resolvedDirectory, error) || error)
    {
        Diagnostics::Debug::LogError(
            "Project directory is missing or invalid. path=", directoryPath.string());
        return std::nullopt;
    }

    std::optional<std::filesystem::path> result;
    std::filesystem::directory_iterator iterator(resolvedDirectory, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end)
    {
        const std::filesystem::directory_entry entry = *iterator;
        iterator.increment(error);

        std::error_code entryError;
        if (!entry.is_regular_file(entryError) || entryError ||
            !HasProjectExtension(entry.path()))
        {
            continue;
        }
        if (result)
        {
            Diagnostics::Debug::LogError(
                "A project directory must contain exactly one .gameproject file. directory=",
                resolvedDirectory.string());
            return std::nullopt;
        }
        result = entry.path().lexically_normal();
    }
    if (error)
    {
        Diagnostics::Debug::LogError(
            "Failed while searching for a project file. directory=", resolvedDirectory.string(),
            ", error=", error.message());
        return std::nullopt;
    }
    if (!result)
    {
        Diagnostics::Debug::LogError(
            "No .gameproject file was found. directory=", resolvedDirectory.string());
    }
    return result;
}

std::optional<ProjectFileData> ProjectFile::Load(const std::filesystem::path& filePath)
{
    std::error_code error;
    const std::filesystem::path resolvedPath =
        std::filesystem::weakly_canonical(filePath, error);
    if (error || !std::filesystem::is_regular_file(resolvedPath, error) || error)
    {
        Diagnostics::Debug::LogError(
            "Project file is missing. path=", filePath.string());
        return std::nullopt;
    }

    const Platform::DirectoryContentSource source(resolvedPath.parent_path());
    return Load(source, resolvedPath.filename());
}

std::optional<ProjectFileData> ProjectFile::Load(
    const Platform::IContentSource& source, const std::filesystem::path& relativePath)
{
    if (!HasProjectExtension(relativePath))
    {
        Diagnostics::Debug::LogError(
            "Project file does not use the .gameproject extension. path=", relativePath.string());
        return std::nullopt;
    }

    std::vector<std::byte> fileBytes;
    if (!source.Read(relativePath, fileBytes))
    {
        Diagnostics::Debug::LogError("Project file is missing. path=", relativePath.string());
        return std::nullopt;
    }

    std::optional<ProjectSettings> settings = ProjectSettingsLoader::Load(fileBytes, relativePath);
    if (!settings)
    {
        return std::nullopt;
    }
    if (ToLower(relativePath.stem().native()) != ToLower(settings->projectName))
    {
        Diagnostics::Debug::LogError(
            "The .gameproject filename must match projectName. file=",
            relativePath.filename().string());
        return std::nullopt;
    }

    // The descriptor's own path is reported the way the caller can use it again: a real path when
    // the project is a directory, and the relative one when it is packed and has none.
    std::filesystem::path filePath = source.ResolveFilePath(relativePath);
    if (filePath.empty())
    {
        filePath = relativePath;
    }
    return ProjectFileData{ std::move(filePath), std::move(*settings) };
}

std::filesystem::path ProjectFile::FindContentRoot(
    const std::filesystem::path& projectDirectory)
{
    std::error_code error;
    for (const std::filesystem::path& candidate :
        { projectDirectory / "Content", projectDirectory })
    {
        if (std::filesystem::is_directory(candidate, error) && FindInDirectory(candidate))
        {
            return candidate;
        }
    }
    return {};
}

std::optional<std::filesystem::path> ProjectFile::FindInSource(
    const Platform::IContentSource& source)
{
    std::optional<std::filesystem::path> result;
    for (const std::filesystem::path& relativePath : source.List())
    {
        // Only the root, which is what makes the descriptor the project's own rather than one of
        // several a content directory happens to contain.
        if (relativePath.has_parent_path() || !HasProjectExtension(relativePath))
        {
            continue;
        }
        if (result)
        {
            Diagnostics::Debug::LogError(
                "A project must contain exactly one .gameproject file. source=",
                source.GetDescription().string());
            return std::nullopt;
        }
        result = relativePath;
    }
    if (!result)
    {
        Diagnostics::Debug::LogError(
            "No .gameproject file was found. source=", source.GetDescription().string());
    }
    return result;
}

std::optional<ProjectFileData> ProjectFile::Create(const std::filesystem::path& filePath)
{
    std::error_code error;
    const std::filesystem::path absolutePath =
        std::filesystem::absolute(filePath, error).lexically_normal();
    if (error || absolutePath.filename().empty() || !HasProjectExtension(absolutePath) ||
        absolutePath.stem().empty())
    {
        Diagnostics::Debug::LogError(
            "New project path must end with <ProjectName>.gameproject. path=",
            filePath.string());
        return std::nullopt;
    }
    if (std::filesystem::exists(absolutePath, error) || error)
    {
        Diagnostics::Debug::LogError(
            "New project file already exists or cannot be inspected. path=",
            absolutePath.string());
        return std::nullopt;
    }

    const std::filesystem::path projectRoot = absolutePath.parent_path();
    std::filesystem::create_directories(projectRoot, error);
    if (error)
    {
        Diagnostics::Debug::LogError(
            "Failed to create the project directory. path=", projectRoot.string(),
            ", error=", error.message());
        return std::nullopt;
    }

    std::filesystem::directory_iterator iterator(projectRoot, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end)
    {
        const std::filesystem::directory_entry entry = *iterator;
        iterator.increment(error);
        if (HasProjectExtension(entry.path()))
        {
            Diagnostics::Debug::LogError(
                "The selected directory already contains a .gameproject file. directory=",
                projectRoot.string());
            return std::nullopt;
        }
    }
    if (error)
    {
        Diagnostics::Debug::LogError(
            "Failed to inspect the new project directory. error=", error.message());
        return std::nullopt;
    }

    const std::filesystem::path scenePath = projectRoot / "Scenes" / "Main.scene";
    if (std::filesystem::exists(scenePath, error) || error)
    {
        Diagnostics::Debug::LogError(
            "New project would overwrite an existing default scene. path=", scenePath.string());
        return std::nullopt;
    }
    std::filesystem::create_directories(scenePath.parent_path(), error);
    if (error)
    {
        Diagnostics::Debug::LogError(
            "Failed to create the default scene directory. error=", error.message());
        return std::nullopt;
    }

    const std::string projectName = PathToUtf8(absolutePath.stem());
    if (projectName.empty())
    {
        Diagnostics::Debug::LogError("New project name is not valid UTF-8.");
        return std::nullopt;
    }
    const std::string escapedProjectName = EscapeJsonString(projectName);
    // 새 프로젝트에는 sourceRootPath를 적지 않는다. 새로 만든 프로젝트는 콘텐츠와 코드가 한
    // 디렉터리이므로 기본값 "."이 곧 옳은 답이고, 옳은 답을 굳이 적으면 그것을 고쳐야 하는
    // 값처럼 보인다. 코드를 다른 자리로 옮기는 사람이 그때 한 줄 적는다.
    const std::string projectContents =
        "{\n"
        "  \"projectName\": \"" + escapedProjectName + "\",\n"
        "  \"window\": {\n"
        "    \"width\": 1280,\n"
        "    \"height\": 720\n"
        "  },\n"
        "  \"targetFrameRate\": 60,\n"
        "  \"initialSceneId\": 0,\n"
        "  \"scenes\": [\n"
        "    { \"id\": 0, \"path\": \"Scenes/Main.scene\" }\n"
        "  ],\n"
        "  \"graphicsApi\": \"Auto\",\n"
        "  \"windowChrome\": \"system\",\n"
        "  \"windowTheme\": \"system\"\n"
        "}\n";
    constexpr std::string_view sceneContents =
        "{\n"
        "  \"sceneName\": \"MainScene\",\n"
        "  \"gameObjects\": [\n"
        "    {\n"
        "      \"name\": \"MainCamera\",\n"
        "      \"isActive\": true,\n"
        "      \"components\": [\n"
        "        {\n"
        "          \"type\": \"Transform\",\n"
        "          \"position\": [0.0, 0.0, -5.0],\n"
        "          \"rotation\": [0.0, 0.0, 0.0],\n"
        "          \"scale\": [1.0, 1.0, 1.0]\n"
        "        },\n"
        "        {\n"
        "          \"type\": \"Camera\",\n"
        "          \"fieldOfView\": 60.0,\n"
        "          \"nearClipPlane\": 0.1,\n"
        "          \"farClipPlane\": 1000.0\n"
        "        }\n"
        "      ]\n"
        "    }\n"
        "  ]\n"
        "}\n";

    std::filesystem::path temporaryProjectPath = absolutePath;
    temporaryProjectPath += L".tmp";
    std::filesystem::path temporaryScenePath = scenePath;
    temporaryScenePath += L".tmp";

    const bool temporaryProjectExists =
        std::filesystem::exists(temporaryProjectPath, error);
    if (error)
    {
        Diagnostics::Debug::LogError(
            "Failed to inspect the temporary project path. error=", error.message());
        return std::nullopt;
    }
    const bool temporarySceneExists = std::filesystem::exists(temporaryScenePath, error);
    if (error)
    {
        Diagnostics::Debug::LogError(
            "Failed to inspect the temporary scene path. error=", error.message());
        return std::nullopt;
    }
    if (temporaryProjectExists || temporarySceneExists)
    {
        Diagnostics::Debug::LogError(
            "New project creation would overwrite a temporary file. projectTemp=",
            temporaryProjectPath.string(), ", sceneTemp=", temporaryScenePath.string());
        return std::nullopt;
    }

    if (!WriteTextFile(temporaryProjectPath, projectContents) ||
        !WriteTextFile(temporaryScenePath, sceneContents))
    {
        std::filesystem::remove(temporaryProjectPath, error);
        std::filesystem::remove(temporaryScenePath, error);
        Diagnostics::Debug::LogError("Failed to write new project files.");
        return std::nullopt;
    }

    std::filesystem::rename(temporaryScenePath, scenePath, error);
    if (error)
    {
        const std::error_code publishError = error;
        std::error_code cleanupError;
        std::filesystem::remove(temporaryProjectPath, cleanupError);
        std::filesystem::remove(temporaryScenePath, cleanupError);
        Diagnostics::Debug::LogError(
            "Failed to publish the default project scene. error=", publishError.message());
        return std::nullopt;
    }
    std::filesystem::rename(temporaryProjectPath, absolutePath, error);
    if (error)
    {
        const std::error_code publishError = error;
        std::error_code cleanupError;
        std::filesystem::remove(scenePath, cleanupError);
        std::filesystem::remove(temporaryProjectPath, cleanupError);
        Diagnostics::Debug::LogError(
            "Failed to publish the new project file. error=", publishError.message());
        return std::nullopt;
    }

    std::optional<ProjectFileData> project = Load(absolutePath);
    if (!project)
    {
        std::filesystem::remove(absolutePath, error);
        std::filesystem::remove(scenePath, error);
        return std::nullopt;
    }
    // 빌드 스크립트도 함께 놓는다. 이름이 식별자가 아니면 못 쓰는데, 그것은 프로젝트가
    // 잘못된 것이 아니라 코드를 담을 수 없는 것뿐이라 만들기를 되돌리지는 않는다 — 이유는
    // WriteBuildScript가 로그로 말한다.
    static_cast<void>(WriteBuildScript(
        projectRoot, projectName, std::filesystem::path(DefaultContentDirectory)));
    Diagnostics::Debug::Log("Created new project. file=", absolutePath.string());
    return project;
}


bool ProjectFile::IsUsableAsIdentifier(const std::string_view name)
{
    if (name.empty())
    {
        return false;
    }
    const auto isLeading = [](const char character)
    {
        return character == '_' || (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z');
    };
    const auto isTrailing = [&isLeading](const char character)
    {
        return isLeading(character) || (character >= '0' && character <= '9');
    };
    if (!isLeading(name.front()) ||
        !std::ranges::all_of(name.substr(1), isTrailing))
    {
        return false;
    }

    // 예약어는 식별자 모양이지만 네임스페이스 이름도 클래스 이름도 될 수 없다. 목록이 아니라
    // 표인 이유는, 빠진 하나가 컴파일 오류로 나타나되 그 오류가 생성된 파일 안에서 나기 때문이다
    // — 사람이 방금 만든 파일이 컴파일되지 않는 것보다 만들 때 거절당하는 편이 낫다.
    static constexpr std::string_view Reserved[] = {
        "alignas", "alignof", "and", "asm", "auto", "bitand", "bitor", "bool", "break", "case",
        "catch", "char", "class", "compl", "concept", "const", "consteval", "constexpr",
        "constinit", "continue", "co_await", "co_return", "co_yield", "decltype", "default",
        "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern",
        "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable",
        "namespace", "new", "noexcept", "not", "nullptr", "operator", "or", "private",
        "protected", "public", "register", "reinterpret_cast", "requires", "return", "short",
        "signed", "sizeof", "static", "static_assert", "static_cast", "struct", "switch",
        "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid",
        "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t",
        "while", "xor" };
    return std::ranges::find(Reserved, name) == std::ranges::end(Reserved);
}

bool ProjectFile::WriteBuildScript(
    const std::filesystem::path& projectRoot, const std::string_view projectName,
    const std::filesystem::path& contentDirectory, const std::filesystem::path& iconPath,
    const std::string_view iconGuid)
{
    const std::filesystem::path scriptPath = projectRoot / "CMakeLists.txt";
    std::error_code error;
    if (std::filesystem::exists(scriptPath, error) && !error)
    {
        // 이미 있는 것은 사람이 손댔을 수 있다. glob이라 파일이 늘어도 고칠 것이 없으므로,
        // 여기서 다시 쓸 이유가 없다.
        return true;
    }
    if (error)
    {
        Diagnostics::Debug::LogError(
            "Failed to inspect the project build script. path=", scriptPath.string(),
            ", error=", error.message());
        return false;
    }
    if (!IsUsableAsIdentifier(projectName))
    {
        Diagnostics::Debug::LogError(
            "The project name cannot be a CMake target or a C++ namespace, so no build script was "
            "written. Rename the project to letters, digits and underscores, starting with a "
            "letter. name=", projectName);
        return false;
    }

    std::string contentValue = PathToUtf8(contentDirectory.generic_u8string().empty()
        ? std::filesystem::path(DefaultContentDirectory) : contentDirectory);
    if (contentValue.empty())
    {
        contentValue = std::string(DefaultContentDirectory);
    }
    const std::string upperName = [projectName]
    {
        std::string upper(projectName);
        std::ranges::transform(upper, upper.begin(), [](const unsigned char character)
        {
            return static_cast<char>(std::toupper(character));
        });
        return upper;
    }();

    // 아이콘 인자의 기본값은 없음이다. 아이콘이 없는 프로젝트에는 아이콘 설정을 출력하지 않는다.
    std::string iconArguments;
    if (!iconPath.empty())
    {
        iconArguments = " ICON \"" + PathToUtf8(iconPath) + "\"";
        if (!iconGuid.empty())
        {
            iconArguments += " ICON_GUID \"" + std::string(iconGuid) + "\"";
        }
    }

    const std::string contents =
        "# This project's own build script. The engine adds it by path, so it needs no engine\n"
        "# location of its own: gameengine_add_game_project is already in scope.\n"
        "file(GLOB " + upperName + "_SOURCES CONFIGURE_DEPENDS\n"
        "    \"${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp\"\n"
        "    \"${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h\")\n"
        "\n"
        "gameengine_add_game_project(" + std::string(projectName) +
        " CONTENT_DIR \"" + contentValue + "\" SOURCES ${" + upperName + "_SOURCES}" +
        iconArguments + ")\n";
    if (!WriteTextFile(scriptPath, contents))
    {
        Diagnostics::Debug::LogError(
            "Failed to write the project build script. path=", scriptPath.string());
        return false;
    }
    Diagnostics::Debug::Log("Wrote a build script for the project. path=", scriptPath.string());
    return true;
}

unsigned int ProjectFile::ChooseSceneId(const ProjectSettings& settings)
{
    unsigned int candidate = 0;
    while (settings.scenePaths.contains(candidate))
    {
        ++candidate;
    }
    return candidate;
}

std::string ProjectFile::MakeSceneContents(const std::string_view sceneName)
{
    // 새 프로젝트의 기본 장면과 같은 모양이다. 카메라가 없으면 장면을 열어도 화면이 비어 있고,
    // 그 빈 화면은 "아직 아무것도 없다"가 아니라 "고장났다"로 읽힌다.
    return
        "{\n"
        "  \"sceneName\": \"" + EscapeJsonString(sceneName) + "\",\n"
        "  \"gameObjects\": [\n"
        "    {\n"
        "      \"name\": \"MainCamera\",\n"
        "      \"isActive\": true,\n"
        "      \"components\": [\n"
        "        {\n"
        "          \"type\": \"Transform\",\n"
        "          \"position\": [0.0, 0.0, -5.0],\n"
        "          \"rotation\": [0.0, 0.0, 0.0],\n"
        "          \"scale\": [1.0, 1.0, 1.0]\n"
        "        },\n"
        "        {\n"
        "          \"type\": \"Camera\",\n"
        "          \"fieldOfView\": 60.0,\n"
        "          \"nearClipPlane\": 0.1,\n"
        "          \"farClipPlane\": 1000.0\n"
        "        }\n"
        "      ]\n"
        "    }\n"
        "  ]\n"
        "}\n";
}

std::string ProjectFile::Serialize(const ProjectSettings& settings)
{
    // 읽는 쪽이 아는 낱말만 쓴다. 두 표가 어긋나면 방금 쓴 파일을 자기가 읽지 못한다.
    const auto chromeName = [](const WindowChrome chrome)
    {
        return chrome == WindowChrome::Custom ? "custom" : "system";
    };
    const auto themeName = [](const WindowTheme theme)
    {
        switch (theme)
        {
        case WindowTheme::Light:
            return "light";
        case WindowTheme::Dark:
            return "dark";
        case WindowTheme::System:
            break;
        }
        return "system";
    };

    // 장면은 ID 순으로 적는다. 순서가 안정적이어야 장면 하나를 더한 diff가 한 줄로 보인다 —
    // 해시 표를 그대로 훑으면 같은 내용이 실행마다 다른 순서로 적힌다.
    std::vector<std::pair<unsigned int, std::filesystem::path>> scenes(
        settings.scenePaths.begin(), settings.scenePaths.end());
    std::sort(
        scenes.begin(), scenes.end(),
        [](const auto& left, const auto& right) { return left.first < right.first; });

    std::string contents =
        "{\n"
        "  \"projectName\": \"" +
        EscapeJsonString(PathToUtf8(std::filesystem::path(settings.projectName))) + "\",\n"
        "  \"window\": {\n"
        "    \"width\": " + std::to_string(settings.windowWidth) + ",\n"
        "    \"height\": " + std::to_string(settings.windowHeight) + "\n"
        "  },\n"
        "  \"targetFrameRate\": " +
        std::to_string(static_cast<int>(settings.targetFrameRate)) + ",\n";

    // 코드 자리는 <b>적혀 있던 프로젝트에만</b> 다시 적는다. 이 함수는 파일을 통째로 새로 쓰므로,
    // 적지 않으면 ".."로 적어 둔 프로젝트가 장면 하나를 더하는 것만으로 그 줄을 잃는다 — 그러면
    // 다음 빌드가 콘텐츠 디렉터리를 CMake에게 넘긴다. 기본값인 프로젝트에 굳이 줄을 더하지 않는
    // 이유는 그 반대다: 안 쓰던 파일이 저장 한 번에 달라지지 않아야 한다.
    if (!settings.sourceRootPath.empty() && settings.sourceRootPath.lexically_normal() != ".")
    {
        contents += "  \"sourceRootPath\": \"" +
            EscapeJsonString(PathToUtf8(settings.sourceRootPath)) + "\",\n";
    }

    contents +=
        "  \"initialSceneId\": " + std::to_string(settings.initialSceneId) + ",\n"
        "  \"scenes\": [\n";
    for (std::size_t index = 0; index < scenes.size(); ++index)
    {
        contents += "    { \"id\": " + std::to_string(scenes[index].first) + ", \"path\": \"" +
            EscapeJsonString(PathToUtf8(scenes[index].second)) + "\" }";
        contents += index + 1 < scenes.size() ? ",\n" : "\n";
    }
    contents +=
        "  ],\n"
        "  \"graphicsApi\": \"" +
        EscapeJsonString(settings.graphicsApi.empty() ? "Auto" : settings.graphicsApi) + "\",\n"
        "  \"windowChrome\": \"" + chromeName(settings.windowChrome) + "\",\n"
        "  \"windowTheme\": \"" + themeName(settings.windowTheme) + "\"\n";

    // 아이콘 없음이 유효한 설정이므로, 없는 프로젝트에는 줄을 더하지 않는다 — sourceRootPath와
    // 같은 이유다.
    if (!settings.icon.empty())
    {
        contents.pop_back();
        contents += ",\n  \"icon\": \"" + EscapeJsonString(settings.icon) + "\"\n";
    }

    contents += "}\n";
    return contents;
}

std::optional<ProjectSettings> ProjectFile::WithScene(
    const ProjectSettings& settings,
    const std::filesystem::path& relativeScenePath,
    const unsigned int sceneId)
{
    if (relativeScenePath.empty() || relativeScenePath.is_absolute())
    {
        Diagnostics::Debug::LogError(
            "A scene must be registered by a path relative to the project. path=",
            relativeScenePath.string());
        return std::nullopt;
    }
    if (settings.scenePaths.contains(sceneId))
    {
        Diagnostics::Debug::LogError("That scene id is already in use. id=", sceneId);
        return std::nullopt;
    }
    const std::string candidate = PathToUtf8(relativeScenePath);
    for (const auto& [existingId, existingPath] : settings.scenePaths)
    {
        if (PathToUtf8(existingPath) == candidate)
        {
            Diagnostics::Debug::LogError(
                "That scene file is already registered. path=", candidate,
                ", id=", existingId);
            return std::nullopt;
        }
    }

    ProjectSettings updated = settings;
    updated.scenePaths.emplace(sceneId, relativeScenePath);
    // initialSceneId는 건드리지 않는다. 장면을 하나 만들었다고 게임이 다른 곳에서 시작하면
    // 그것은 사람이 요청하지 않은 변경이다.
    return updated;
}

std::optional<ProjectFileData> ProjectFile::AddScene(
    const ProjectFileData& project, const std::filesystem::path& sceneFilePath)
{
    std::error_code error;
    const std::filesystem::path absoluteScenePath =
        std::filesystem::absolute(sceneFilePath, error).lexically_normal();
    if (error || absoluteScenePath.stem().empty() ||
        ToLower(absoluteScenePath.extension().native()) != L".scene")
    {
        Diagnostics::Debug::LogError(
            "A new scene path must end with <SceneName>.scene. path=", sceneFilePath.string());
        return std::nullopt;
    }

    const std::filesystem::path projectRoot =
        project.filePath.parent_path().lexically_normal();
    // 프로젝트 밖의 장면은 등록할 수 없다: 경로가 프로젝트 기준 상대 경로로 배포되므로, 밖을
    // 가리키면 빌드된 게임에는 그 파일이 없다.
    const std::optional<std::filesystem::path> relative =
        Platform::RelativePathWithin(projectRoot, absoluteScenePath);
    if (!relative)
    {
        Diagnostics::Debug::LogError(
            "A new scene must live inside the project folder. scene=",
            absoluteScenePath.string(), ", project=", projectRoot.string());
        return std::nullopt;
    }
    if (std::filesystem::exists(absoluteScenePath, error) || error)
    {
        Diagnostics::Debug::LogError(
            "A file already exists at that scene path. path=", absoluteScenePath.string());
        return std::nullopt;
    }

    const unsigned int sceneId = ChooseSceneId(project.settings);
    const std::optional<ProjectSettings> updated =
        WithScene(project.settings, *relative, sceneId);
    if (!updated)
    {
        return std::nullopt;
    }

    std::filesystem::create_directories(absoluteScenePath.parent_path(), error);
    if (error)
    {
        Diagnostics::Debug::LogError(
            "Failed to create the scene directory. error=", error.message());
        return std::nullopt;
    }

    const std::string sceneName = PathToUtf8(absoluteScenePath.stem());
    if (sceneName.empty())
    {
        Diagnostics::Debug::LogError("The new scene name is not valid UTF-8.");
        return std::nullopt;
    }

    // 장면 파일을 먼저 쓰고 프로젝트 파일을 마지막에 바꾼다. 순서가 반대이면 중간에 실패했을 때
    // 프로젝트가 없는 장면을 가리키게 되고, 그런 프로젝트는 아예 열리지 않는다. 이 순서에서는
    // 최악이 "등록되지 않은 파일 하나"이고 그것은 아무것도 망가뜨리지 않는다.
    std::filesystem::path temporaryScenePath = absoluteScenePath;
    temporaryScenePath += L".tmp";
    std::filesystem::path temporaryProjectPath = project.filePath;
    temporaryProjectPath += L".tmp";
    if (std::filesystem::exists(temporaryScenePath, error) ||
        std::filesystem::exists(temporaryProjectPath, error) || error)
    {
        Diagnostics::Debug::LogError(
            "Adding a scene would overwrite a temporary file. scene=",
            temporaryScenePath.string(), ", project=", temporaryProjectPath.string());
        return std::nullopt;
    }

    if (!WriteTextFile(temporaryScenePath, MakeSceneContents(sceneName)) ||
        !WriteTextFile(temporaryProjectPath, Serialize(*updated)))
    {
        std::error_code cleanupError;
        std::filesystem::remove(temporaryScenePath, cleanupError);
        std::filesystem::remove(temporaryProjectPath, cleanupError);
        Diagnostics::Debug::LogError("Failed to write the new scene files.");
        return std::nullopt;
    }

    std::filesystem::rename(temporaryScenePath, absoluteScenePath, error);
    if (error)
    {
        const std::error_code publishError = error;
        std::error_code cleanupError;
        std::filesystem::remove(temporaryScenePath, cleanupError);
        std::filesystem::remove(temporaryProjectPath, cleanupError);
        Diagnostics::Debug::LogError(
            "Failed to publish the new scene. error=", publishError.message());
        return std::nullopt;
    }
    std::filesystem::rename(temporaryProjectPath, project.filePath, error);
    if (error)
    {
        const std::error_code publishError = error;
        std::error_code cleanupError;
        std::filesystem::remove(absoluteScenePath, cleanupError);
        std::filesystem::remove(temporaryProjectPath, cleanupError);
        Diagnostics::Debug::LogError(
            "Failed to register the new scene. error=", publishError.message());
        return std::nullopt;
    }

    // 다시 읽어 검증한다: 방금 쓴 파일을 우리가 읽지 못한다면, 그것은 사람이 열어 보기 전에
    // 알아야 하는 사실이다. Create도 같은 자리에서 같은 일을 한다.
    std::optional<ProjectFileData> reloaded = Load(project.filePath);
    if (!reloaded)
    {
        Diagnostics::Debug::LogError(
            "The scene was added but the project no longer loads. path=",
            project.filePath.string());
        return std::nullopt;
    }
    Diagnostics::Debug::Log(
        "Added a scene. path=", PathToUtf8(*relative), ", id=", sceneId);
    return reloaded;
}

std::optional<ProjectSettings> ProjectFile::WithoutScene(
    const ProjectSettings& settings, const unsigned int sceneId)
{
    const auto entry = settings.scenePaths.find(sceneId);
    if (entry == settings.scenePaths.end())
    {
        Diagnostics::Debug::LogError("The project has no scene with this id. id=", sceneId);
        return std::nullopt;
    }
    if (settings.scenePaths.size() <= 1)
    {
        // 장면이 없는 프로젝트는 IsValid가 거부한다. 지우는 동작이 프로젝트를 못 여는 상태로
        // 만드는 것은 지우기가 할 일이 아니다.
        Diagnostics::Debug::LogError(
            "A project must keep at least one scene. Removing the last one would make it "
            "unopenable.");
        return std::nullopt;
    }

    ProjectSettings updated = settings;
    updated.scenePaths.erase(sceneId);
    if (updated.initialSceneId == sceneId)
    {
        // 시작 장면이 사라졌다. 그대로 두면 없는 것을 가리켜 프로젝트가 열리지 않으므로, 남은
        // 것 중 가장 작은 ID로 옮긴다. 조용히 바뀌는 값이라 부르는 쪽이 사람에게 알려야 한다.
        unsigned int lowest = (std::numeric_limits<unsigned int>::max)();
        for (const auto& [remainingId, remainingPath] : updated.scenePaths)
        {
            lowest = (std::min)(lowest, remainingId);
        }
        updated.initialSceneId = lowest;
    }
    return updated;
}

std::optional<ProjectSettings> ProjectFile::WithRenamedScene(
    const ProjectSettings& settings,
    const unsigned int sceneId,
    const std::filesystem::path& newRelativePath)
{
    if (newRelativePath.empty() || newRelativePath.is_absolute())
    {
        Diagnostics::Debug::LogError(
            "A scene must be registered by a path relative to the project. path=",
            newRelativePath.string());
        return std::nullopt;
    }
    if (!settings.scenePaths.contains(sceneId))
    {
        Diagnostics::Debug::LogError("The project has no scene with this id. id=", sceneId);
        return std::nullopt;
    }
    const std::string candidate = PathToUtf8(newRelativePath);
    for (const auto& [existingId, existingPath] : settings.scenePaths)
    {
        if (existingId != sceneId && PathToUtf8(existingPath) == candidate)
        {
            Diagnostics::Debug::LogError(
                "Another scene is already registered at that path. path=", candidate);
            return std::nullopt;
        }
    }

    ProjectSettings updated = settings;
    // ID는 그대로다. 바꾸면 initialSceneId가 딸려 와야 하고, 이름을 고치는 일이 시작 장면을
    // 옮기는 일이 되어서는 안 된다.
    updated.scenePaths[sceneId] = newRelativePath;
    return updated;
}

bool ProjectFile::SceneFileExists(const ProjectFileData& project, const unsigned int sceneId)
{
    const auto entry = project.settings.scenePaths.find(sceneId);
    if (entry == project.settings.scenePaths.end())
    {
        return false;
    }
    std::error_code error;
    const std::filesystem::path absolutePath =
        (project.filePath.parent_path() / entry->second).lexically_normal();
    return std::filesystem::is_regular_file(absolutePath, error) && !error;
}

std::optional<ProjectFileData> ProjectFile::RemoveScene(
    const ProjectFileData& project, const unsigned int sceneId)
{
    const auto entry = project.settings.scenePaths.find(sceneId);
    if (entry == project.settings.scenePaths.end())
    {
        Diagnostics::Debug::LogError("The project has no scene with this id. id=", sceneId);
        return std::nullopt;
    }
    const std::optional<ProjectSettings> updated = WithoutScene(project.settings, sceneId);
    if (!updated)
    {
        return std::nullopt;
    }

    const std::filesystem::path projectRoot = project.filePath.parent_path().lexically_normal();
    const std::filesystem::path absoluteScenePath =
        (projectRoot / entry->second).lexically_normal();

    // 등록을 먼저 지운다. 파일부터 지우면 중간 실패 시 등록이 없는 파일을 가리킨다.
    // 이 순서에서는 실패해도 주인 없는 파일만 남으므로 다시 등록할 수 있다.
    std::filesystem::path temporaryProjectPath = project.filePath;
    temporaryProjectPath += L".tmp";
    std::error_code error;
    if (std::filesystem::exists(temporaryProjectPath, error) || error)
    {
        Diagnostics::Debug::LogError(
            "Removing a scene would overwrite a temporary file. path=",
            temporaryProjectPath.string());
        return std::nullopt;
    }
    if (!WriteTextFile(temporaryProjectPath, Serialize(*updated)))
    {
        std::error_code cleanupError;
        std::filesystem::remove(temporaryProjectPath, cleanupError);
        Diagnostics::Debug::LogError("Failed to write the updated project descriptor.");
        return std::nullopt;
    }
    std::filesystem::rename(temporaryProjectPath, project.filePath, error);
    if (error)
    {
        const std::error_code publishError = error;
        std::error_code cleanupError;
        std::filesystem::remove(temporaryProjectPath, cleanupError);
        Diagnostics::Debug::LogError(
            "Failed to publish the updated project descriptor. error=", publishError.message());
        return std::nullopt;
    }

    // 등록이 사라진 뒤의 파일 삭제다. 실패해도 프로젝트는 온전하다 — 파일 하나가 주인 없이
    // 남을 뿐이고, 그 사실은 로그로 남긴다.
    std::filesystem::remove(absoluteScenePath, error);
    if (error)
    {
        Diagnostics::Debug::LogError(
            "The scene was unregistered but its file could not be deleted. path=",
            absoluteScenePath.string(), ", error=", error.message());
    }

    std::optional<ProjectFileData> reloaded = Load(project.filePath);
    if (!reloaded)
    {
        Diagnostics::Debug::LogError(
            "The scene was removed but the project no longer loads. path=",
            project.filePath.string());
        return std::nullopt;
    }
    Diagnostics::Debug::Log(
        "Removed a scene. path=", PathToUtf8(entry->second), ", id=", sceneId);
    return reloaded;
}

std::optional<ProjectFileData> ProjectFile::RenameScene(
    const ProjectFileData& project,
    const unsigned int sceneId,
    const std::filesystem::path& sceneFilePath)
{
    const auto entry = project.settings.scenePaths.find(sceneId);
    if (entry == project.settings.scenePaths.end())
    {
        Diagnostics::Debug::LogError("The project has no scene with this id. id=", sceneId);
        return std::nullopt;
    }

    std::error_code error;
    const std::filesystem::path absoluteNewPath =
        std::filesystem::absolute(sceneFilePath, error).lexically_normal();
    if (error || absoluteNewPath.stem().empty() ||
        ToLower(absoluteNewPath.extension().native()) != L".scene")
    {
        Diagnostics::Debug::LogError(
            "A scene path must end with <SceneName>.scene. path=", sceneFilePath.string());
        return std::nullopt;
    }

    const std::filesystem::path projectRoot = project.filePath.parent_path().lexically_normal();
    const std::optional<std::filesystem::path> relative =
        Platform::RelativePathWithin(projectRoot, absoluteNewPath);
    if (!relative)
    {
        Diagnostics::Debug::LogError(
            "A scene must live inside the project folder. scene=", absoluteNewPath.string(),
            ", project=", projectRoot.string());
        return std::nullopt;
    }
    const std::filesystem::path absoluteOldPath = (projectRoot / entry->second).lexically_normal();
    if (absoluteOldPath == absoluteNewPath)
    {
        // 같은 이름으로 바꾸는 것은 아무 일도 아니다. 실패로 다루면 부르는 쪽이 오류를 보여야
        // 하는데, 사람이 한 일은 잘못된 것이 없다.
        return project;
    }
    if (std::filesystem::exists(absoluteNewPath, error) || error)
    {
        Diagnostics::Debug::LogError(
            "A file already exists at that scene path. path=", absoluteNewPath.string());
        return std::nullopt;
    }

    const std::optional<ProjectSettings> updated =
        WithRenamedScene(project.settings, sceneId, *relative);
    if (!updated)
    {
        return std::nullopt;
    }

    std::filesystem::create_directories(absoluteNewPath.parent_path(), error);
    if (error)
    {
        Diagnostics::Debug::LogError(
            "Failed to create the scene directory. error=", error.message());
        return std::nullopt;
    }

    // 파일을 먼저 옮긴다. 옮기다 실패하면 아무것도 바뀌지 않았고, 옮긴 뒤 등록에 실패하면 파일을
    // 제자리로 되돌릴 수 있다 — 지우기와 달리 원본이 아직 존재하기 때문이다.
    std::filesystem::rename(absoluteOldPath, absoluteNewPath, error);
    if (error)
    {
        Diagnostics::Debug::LogError(
            "Failed to rename the scene file. error=", error.message());
        return std::nullopt;
    }

    std::filesystem::path temporaryProjectPath = project.filePath;
    temporaryProjectPath += L".tmp";
    const auto putTheFileBack = [&absoluteOldPath, &absoluteNewPath, &temporaryProjectPath]()
    {
        std::error_code undoError;
        std::filesystem::rename(absoluteNewPath, absoluteOldPath, undoError);
        std::filesystem::remove(temporaryProjectPath, undoError);
    };
    if (std::filesystem::exists(temporaryProjectPath, error) || error ||
        !WriteTextFile(temporaryProjectPath, Serialize(*updated)))
    {
        putTheFileBack();
        Diagnostics::Debug::LogError(
            "Failed to write the updated project descriptor; the scene was left as it was.");
        return std::nullopt;
    }
    std::filesystem::rename(temporaryProjectPath, project.filePath, error);
    if (error)
    {
        const std::error_code publishError = error;
        putTheFileBack();
        Diagnostics::Debug::LogError(
            "Failed to publish the updated project descriptor; the scene was left as it was. "
            "error=", publishError.message());
        return std::nullopt;
    }

    std::optional<ProjectFileData> reloaded = Load(project.filePath);
    if (!reloaded)
    {
        Diagnostics::Debug::LogError(
            "The scene was renamed but the project no longer loads. path=",
            project.filePath.string());
        return std::nullopt;
    }
    Diagnostics::Debug::Log(
        "Renamed a scene. from=", PathToUtf8(entry->second),
        ", to=", PathToUtf8(*relative), ", id=", sceneId);
    return reloaded;
}

bool ProjectFile::HasProjectExtension(const std::filesystem::path& filePath)
{
    return ToLower(filePath.extension().native()) == Extension;
}

}
