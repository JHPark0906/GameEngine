#include "pch.h"
#include "EditorSettings.h"

#include <fstream>
#include <sstream>
#include <optional>
#include <string>
#include <utility>

#include "../Core/Json.h"
#include "../Platform/TextFile.h"
#include "GraphicsBackendChoice.h"

namespace GameEngine::App
{

namespace
{
    using Core::Json;

    [[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string value = path.generic_u8string();
        return {
            reinterpret_cast<const char*>(value.data()),
            reinterpret_cast<const char*>(value.data() + value.size())
        };
    }

    [[nodiscard]] std::filesystem::path PathFromUtf8(const std::string& text)
    {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }

    /// <summary>있으면 세 실수의 배열을 읽는다. 형태가 다르면 false — 호출자는 기본값을 남긴다.</summary>
    [[nodiscard]] bool TryReadVector3(const Json& parent, const std::string& key, Math::Vector3& out)
    {
        const Json* const value = parent.Find(key);
        if (!value || !value->IsArray() || value->Size() != 3)
        {
            return false;
        }
        try
        {
            out = { value->At(0).Get<float>(), value->At(1).Get<float>(),
                value->At(2).Get<float>() };
            return true;
        }
        catch (const Core::JsonError&)
        {
            return false;
        }
    }
}

std::string EditorSettings::ToText(const EditorSettingsData& data)
{
    Json::Object root;
    root.emplace("version", Json(static_cast<double>(Version)));

    if (!data.panelInSlot.empty())
    {
        Json::Array slots;
        slots.reserve(data.panelInSlot.size());
        for (const std::size_t panelIndex : data.panelInSlot)
        {
            slots.emplace_back(static_cast<double>(panelIndex));
        }
        root.emplace("panelSlots", Json(std::move(slots)));
    }
    if (!data.lastProjectPath.empty())
    {
        root.emplace("lastProjectPath", Json(PathToUtf8(data.lastProjectPath)));
    }
    if (data.hasLastScene)
    {
        root.emplace("lastSceneId", Json(static_cast<double>(data.lastSceneId)));
    }
    // 백엔드 설정은 기본값이어도 늘 쓰고, 쓸 수 있는 값의 목록을 옆에 함께 적는다. JSON에는
    // 주석이 없으므로 그 목록이 설명 자리를 대신한다 — 파일을 연 사람이 "Select"라는 값이
    // 있다는 것을 여기서 알게 된다. 목록은 읽을 때 무시되며, 이 빌드가 실제로 가진 백엔드를
    // 담으므로 백엔드가 늘거나 줄면 파일도 따라 바뀐다.
    root.emplace("graphicsApi", Json(data.graphicsApi));
    Json::Array choices;
    for (std::string& value : ListGraphicsBackendSettingValues())
    {
        choices.emplace_back(std::move(value));
    }
    root.emplace("graphicsApiChoices", Json(std::move(choices)));
    // 콘솔 창도 떠 있을 때만 쓴다. 도킹은 기본 상태이므로 적을 것이 없다.
    if (data.consoleFloating)
    {
        root.emplace("consoleFloating", Json(true));
        root.emplace("consoleWindow", Json(Json::Array{
            Json(static_cast<double>(data.consoleWindowX)),
            Json(static_cast<double>(data.consoleWindowY)),
            Json(static_cast<double>(data.consoleWindowWidth)),
            Json(static_cast<double>(data.consoleWindowHeight)) }));
    }
    // 스냅은 켜졌을 때만 쓴다: 꺼진 기본 상태의 설정 파일은 이 키들을 갖지 않는다.
    if (data.gridSnapEnabled)
    {
        root.emplace("gridSnapEnabled", Json(true));
    }
    if (data.gridSnapSpacing != EditorSettingsData{}.gridSnapSpacing)
    {
        root.emplace("gridSnapSpacing", Json(static_cast<double>(data.gridSnapSpacing)));
    }
    if (data.hasSceneCamera)
    {
        Json::Object camera;
        camera.emplace("pivot", Json(Json::Array{
            Json(static_cast<double>(data.cameraPivot.GetX())),
            Json(static_cast<double>(data.cameraPivot.GetY())),
            Json(static_cast<double>(data.cameraPivot.GetZ())) }));
        camera.emplace("distance", Json(static_cast<double>(data.cameraDistance)));
        camera.emplace("yawDegrees", Json(static_cast<double>(data.cameraYawDegrees)));
        camera.emplace("pitchDegrees", Json(static_cast<double>(data.cameraPitchDegrees)));
        root.emplace("sceneCamera", Json(std::move(camera)));
    }
    return Json(std::move(root)).Dump();
}

EditorSettingsData EditorSettings::FromText(const std::string_view text)
{
    EditorSettingsData data;

    Json root;
    try
    {
        root = Json::Parse(text);
    }
    catch (const Core::JsonError&)
    {
        return data;
    }
    if (!root.IsObject() || root.Value("version", 0) != Version)
    {
        return data;
    }

    if (const Json* const slots = root.Find("panelSlots"); slots && slots->IsArray())
    {
        try
        {
            std::vector<std::size_t> panelInSlot;
            panelInSlot.reserve(slots->Size());
            for (const Json& entry : slots->AsArray())
            {
                panelInSlot.push_back(entry.Get<unsigned int>());
            }
            data.panelInSlot = std::move(panelInSlot);
        }
        catch (const Core::JsonError&)
        {
            // 배열의 일부만 숫자인 파일이다. 반쪽 배정은 배정이 아니므로 통째로 기본값이다.
            data.panelInSlot.clear();
        }
    }

    const std::string lastProjectPath = root.Value("lastProjectPath", std::string{});
    if (!lastProjectPath.empty())
    {
        data.lastProjectPath = PathFromUtf8(lastProjectPath);
    }
    if (const Json* const lastSceneId = root.Find("lastSceneId"))
    {
        try
        {
            data.lastSceneId = lastSceneId->Get<unsigned int>();
            data.hasLastScene = true;
        }
        catch (const Core::JsonError&)
        {
        }
    }

    // 빈 값은 키가 없는 것과 같이 다룬다 — 무엇으로 열지 말하지 않은 파일이므로 기본값이다.
    if (std::string graphicsApi = root.Value("graphicsApi", std::string{}); !graphicsApi.empty())
    {
        data.graphicsApi = std::move(graphicsApi);
    }

    // 떠 있다고만 적히고 자리가 없거나 망가진 파일은 도킹으로 읽는다: 자리를 모르는 창을
    // 띄우면 어디에 서는지 아무도 답할 수 없고, 도킹은 언제나 성립하는 상태다.
    if (root.Value("consoleFloating", false))
    {
        if (const Json* const window = root.Find("consoleWindow");
            window && window->IsArray() && window->Size() == 4)
        {
            try
            {
                const float x = window->At(0).Get<float>();
                const float y = window->At(1).Get<float>();
                const float width = window->At(2).Get<float>();
                const float height = window->At(3).Get<float>();
                if (width > 0.0f && height > 0.0f)
                {
                    data.consoleWindowX = x;
                    data.consoleWindowY = y;
                    data.consoleWindowWidth = width;
                    data.consoleWindowHeight = height;
                    data.consoleFloating = true;
                }
            }
            catch (const std::exception&)
            {
                // 수가 아닌 값이 적혀 있다. 도킹으로 남는다.
            }
        }
    }

    data.gridSnapEnabled = root.Value("gridSnapEnabled", false);
    if (const float spacing = root.Value("gridSnapSpacing", data.gridSnapSpacing); spacing > 0.0f)
    {
        data.gridSnapSpacing = spacing;
    }

    if (const Json* const camera = root.Find("sceneCamera"); camera && camera->IsObject())
    {
        Math::Vector3 pivot;
        if (TryReadVector3(*camera, "pivot", pivot) && camera->Find("distance") &&
            camera->Find("yawDegrees") && camera->Find("pitchDegrees"))
        {
            try
            {
                data.cameraDistance = camera->At("distance").Get<float>();
                data.cameraYawDegrees = camera->At("yawDegrees").Get<float>();
                data.cameraPitchDegrees = camera->At("pitchDegrees").Get<float>();
                data.cameraPivot = pivot;
                data.hasSceneCamera = true;
            }
            catch (const Core::JsonError&)
            {
                // 반쪽 카메라는 카메라가 아니다. 기본 시점이 낫다.
            }
        }
    }

    return data;
}

EditorSettingsData EditorSettings::Load(const std::filesystem::path& filePath)
{
    const std::optional<std::string> contents = Platform::ReadTextFile(filePath);
    return contents ? FromText(*contents) : EditorSettingsData{};
}

bool EditorSettings::Save(const EditorSettingsData& data, const std::filesystem::path& filePath)
{
    return static_cast<bool>(Platform::WriteTextFile(filePath, ToText(data)));
}

}
