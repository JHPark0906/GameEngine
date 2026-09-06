#include "pch.h"
#include "GraphicsBackend.h"

#include <algorithm>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The one place in the engine that names concrete graphics backends. Adding an API means adding its
// files and one entry here; removing Direct3D for another platform means editing this list alone.
#include "D3D11/D3D11Backend.h"
#include "D3D12/D3D12Backend.h"
#include "../Diagnostics/Debug.h"

namespace GameEngine::Rendering
{

namespace
{
    [[nodiscard]] const std::vector<GraphicsBackendDescriptor>& GetRegisteredBackends()
    {
        static const std::vector<GraphicsBackendDescriptor> backends = []
        {
            std::vector<GraphicsBackendDescriptor> registered{
                D3D11::GetGraphicsBackendDescriptor(),
                D3D12::GetGraphicsBackendDescriptor(),
            };
            std::erase_if(registered, [](const GraphicsBackendDescriptor& descriptor)
            {
                if (descriptor.IsComplete())
                {
                    return false;
                }
                Diagnostics::Debug::LogError(
                    "A graphics backend descriptor is incomplete and was ignored. id=", descriptor.id);
                return true;
            });
            std::ranges::stable_sort(
                registered,
                [](const GraphicsBackendDescriptor& left, const GraphicsBackendDescriptor& right)
                {
                    return left.automaticSelectionPriority > right.automaticSelectionPriority;
                });
            return registered;
        }();
        return backends;
    }
}

std::span<const GraphicsBackendDescriptor> GraphicsBackendRegistry::GetBackends()
{
    return GetRegisteredBackends();
}

const GraphicsBackendDescriptor* GraphicsBackendRegistry::Find(const std::string_view id)
{
    const std::vector<GraphicsBackendDescriptor>& backends = GetRegisteredBackends();
    const auto match = std::ranges::find(backends, id, &GraphicsBackendDescriptor::id);
    return match != backends.end() ? &*match : nullptr;
}

bool GraphicsBackendRegistry::IsKnownId(const std::string_view id)
{
    return id.empty() || id == AutomaticGraphicsBackendId || Find(id) != nullptr;
}

std::string GraphicsBackendRegistry::DescribeAvailableIds()
{
    std::string description(AutomaticGraphicsBackendId);
    for (const GraphicsBackendDescriptor& backend : GetRegisteredBackends())
    {
        description += ", ";
        description += backend.id;
    }
    return description;
}

}
