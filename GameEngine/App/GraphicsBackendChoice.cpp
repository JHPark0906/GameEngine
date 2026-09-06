#include "pch.h"
#include "GraphicsBackendChoice.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../Diagnostics/Debug.h"
#include "../Rendering/GraphicsBackend.h"

namespace GameEngine::App
{

std::vector<GraphicsBackendChoice> ListGraphicsBackendChoices()
{
    std::vector<GraphicsBackendChoice> choices;
    for (const Rendering::GraphicsBackendDescriptor& backend :
         Rendering::GraphicsBackendRegistry::GetBackends())
    {
        choices.push_back({ std::string(backend.id), backend.IsSupported() });
    }
    return choices;
}

std::string DescribeGraphicsBackendChoice(const GraphicsBackendChoice& choice)
{
    return choice.supported ? choice.id : choice.id + " (unsupported here)";
}

bool RequestsGraphicsBackendDialog(const std::string_view id)
{
    return id == SelectGraphicsBackendId;
}

std::string DescribeGraphicsBackendSetting(const std::string_view id)
{
    if (RequestsGraphicsBackendDialog(id))
    {
        return std::string(id) + " (the dialog asks at startup)";
    }
    if (id.empty() || id == Rendering::AutomaticGraphicsBackendId)
    {
        return std::string(Rendering::AutomaticGraphicsBackendId) +
            " (the engine picks the first supported backend in registry order)";
    }
    return std::string(id) + " (requested by name)";
}

std::vector<std::string> ListGraphicsBackendSettingValues()
{
    std::vector<std::string> values;
    for (const GraphicsBackendChoice& choice : ListGraphicsBackendChoices())
    {
        values.push_back(choice.id);
    }
    values.emplace_back(Rendering::AutomaticGraphicsBackendId);
    values.emplace_back(SelectGraphicsBackendId);
    return values;
}

bool ApplyGraphicsBackendChoice(
    ProjectSettings& settings,
    const std::span<const GraphicsBackendChoice> choices,
    const std::optional<std::size_t> chosen)
{
    if (!chosen)
    {
        Diagnostics::Debug::Log("No graphics backend was chosen; the game will not start.");
        return false;
    }
    if (*chosen >= choices.size())
    {
        Diagnostics::Debug::LogError(
            "The chosen graphics backend index is out of range. chosen=", *chosen,
            ", choices=", choices.size());
        return false;
    }
    settings.graphicsApi = choices[*chosen].id;
    Diagnostics::Debug::Log("Graphics backend chosen. requested=", settings.graphicsApi);
    return true;
}

std::optional<std::string> FindGraphicsBackendArgument(
    const std::span<const std::string> arguments)
{
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        if (arguments[index] != GraphicsBackendOption)
        {
            continue;
        }
        return index + 1 < arguments.size() ? arguments[index + 1] : std::string{};
    }
    return std::nullopt;
}

bool ApplyRequestedGraphicsBackend(ProjectSettings& settings, const std::string_view id)
{
    if (id.empty())
    {
        Diagnostics::Debug::LogError(
            "The graphics backend option needs an identifier. available=",
            Rendering::GraphicsBackendRegistry::DescribeAvailableIds());
        return false;
    }
    if (RequestsGraphicsBackendDialog(id))
    {
        // 명령줄은 사람이 없는 실행을 위한 길이다. 여기서 묻기를 요구하면 그 실행이 멈춘다.
        Diagnostics::Debug::LogError(
            "The graphics backend option names a backend, not a choice policy. requested=", id,
            ", available=", Rendering::GraphicsBackendRegistry::DescribeAvailableIds());
        return false;
    }
    if (!Rendering::GraphicsBackendRegistry::IsKnownId(id))
    {
        Diagnostics::Debug::LogError(
            "The requested graphics backend is not part of this build. requested=", id,
            ", available=", Rendering::GraphicsBackendRegistry::DescribeAvailableIds());
        return false;
    }
    settings.graphicsApi = std::string(id);
    Diagnostics::Debug::Log("Graphics backend taken from the command line. requested=", id);
    return true;
}

}
