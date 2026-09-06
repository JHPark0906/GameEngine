#include "pch.h"
#include "ProjectSettingsLoader.h"

#include "../Rendering/GraphicsBackend.h"
#include "../Core/Json.h"
#include "../Diagnostics/Debug.h"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

using Json = GameEngine::Core::Json;

namespace GameEngine::App
{

namespace
{
    std::optional<WindowChrome> ParseWindowChrome(const std::string& value)
    {
        if (value == "system")
        {
            return WindowChrome::System;
        }
        if (value == "custom")
        {
            return WindowChrome::Custom;
        }
        return std::nullopt;
    }

    std::optional<WindowTheme> ParseWindowTheme(const std::string& value)
    {
        if (value == "system") return WindowTheme::System;
        if (value == "light") return WindowTheme::Light;
        if (value == "dark") return WindowTheme::Dark;
        return std::nullopt;
    }

    std::wstring Utf8ToWideString(const std::string& value)
    {
        const std::u8string utf8Value(
            reinterpret_cast<const char8_t*>(value.data()),
            value.size());
        return std::filesystem::path(utf8Value).wstring();
    }
}

std::optional<ProjectSettings> ProjectSettingsLoader::Load(
    const std::span<const std::byte> fileBytes, const std::filesystem::path&)
{
    try
    {
        const Json jsonRoot = Json::ParseBytes(fileBytes);

        ProjectSettings settings;
        settings.projectName = Utf8ToWideString(jsonRoot.At("projectName").Get<std::string>());

        const Json& window = jsonRoot.At("window");
        settings.windowWidth = window.At("width").Get<int>();
        settings.windowHeight = window.At("height").Get<int>();
        settings.targetFrameRate = jsonRoot.Value("targetFrameRate", settings.targetFrameRate);
        // assetRootPath는 지원하지 않는 필드다. 에셋 루트는 프로젝트 파일이 있는 자리다.
        // 호환성을 위해 이 필드가 남아 있는 파일도 열되, 필드를 무시한다는 사실을 한 번 알린다.
        if (const std::string legacyAssetRootPath = jsonRoot.Value("assetRootPath", std::string{});
            !legacyAssetRootPath.empty())
        {
            Diagnostics::Debug::LogWarning(
                "assetRootPath is no longer read; the asset root is always the project file's own "
                "directory. Remove the field from the project file. value=", legacyAssetRootPath);
        }
        // sourceRootPath가 없으면 프로젝트 파일이 있는 자리를 코드 루트로 사용한다.
        settings.sourceRootPath = jsonRoot.Value("sourceRootPath", std::string{});
        settings.initialSceneId = jsonRoot.At("initialSceneId").Get<unsigned int>();

        // The identifier is carried through as written. Which backends exist is a Rendering concern,
        // so it is validated where the device is created rather than by this loader.
        settings.graphicsApi = jsonRoot.Value(
            "graphicsApi", std::string(Rendering::AutomaticGraphicsBackendId));

        const std::optional<WindowChrome> windowChrome = ParseWindowChrome(
            jsonRoot.Value("windowChrome", std::string("system")));
        if (!windowChrome)
        {
            Diagnostics::Debug::LogError("Unsupported window chrome in ProjectSettings.");
            return std::nullopt;
        }
        settings.windowChrome = *windowChrome;

        const std::optional<WindowTheme> windowTheme = ParseWindowTheme(
            jsonRoot.Value("windowTheme", std::string("system")));
        if (!windowTheme)
        {
            Diagnostics::Debug::LogError("Unsupported window theme in ProjectSettings.");
            return std::nullopt;
        }
        settings.windowTheme = *windowTheme;

        settings.icon = jsonRoot.Value("icon", std::string{});

        for (const Json& scene : jsonRoot.At("scenes").AsArray())
        {
            const unsigned int sceneId = scene.At("id").Get<unsigned int>();
            const std::filesystem::path scenePath = scene.At("path").Get<std::string>();

            if (scenePath.empty() || !settings.scenePaths.emplace(sceneId, scenePath).second)
            {
                Diagnostics::Debug::LogError("Invalid or duplicate scene entry. sceneId=", sceneId);
                return std::nullopt;
            }
        }

        if (!settings.IsValid())
        {
            Diagnostics::Debug::LogError("ProjectSettings contains invalid values.");
            return std::nullopt;
        }

        return settings;
    }
    catch (const std::exception& exception)
    {
        Diagnostics::Debug::LogError("Failed to parse ProjectSettings: ", exception.what());
        return std::nullopt;
    }
}

}
