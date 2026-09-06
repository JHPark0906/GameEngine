#include "pch.h"
#include "GraphicsDeviceFactory.h"

#include "GraphicsBackend.h"
#include "../Diagnostics/Debug.h"

#include <memory>
#include <string>
#include <string_view>

namespace GameEngine::Rendering
{

namespace
{
    [[nodiscard]] bool IsAutomatic(const std::string_view requestedId)
    {
        return requestedId.empty() || requestedId == AutomaticGraphicsBackendId;
    }

    /// <summary>이 머신이 지원하는 최우선 백엔드이며, 쓸 수 있는 것이 없으면 null이다.</summary>
    [[nodiscard]] const GraphicsBackendDescriptor* SelectAutomatically()
    {
        return GraphicsDeviceFactory::ChooseAutomatically(GraphicsBackendRegistry::GetBackends())
            .backend;
    }
}

AutomaticBackendChoice GraphicsDeviceFactory::ChooseAutomatically(
    const std::span<const GraphicsBackendDescriptor> backends)
{
    AutomaticBackendChoice choice;
    for (const GraphicsBackendDescriptor& backend : backends)
    {
        // IsSupported는 이름과 달리 표를 뒤지지 않는다: 이 머신에서 그 API의 장치를 실제로
        // 만들어 보고 그 결과를 답한다. 그래서 「지원한다」는 곧 「만들어진다」이며, 여기서
        // 내려가는 것이 곧 생성 실패에 대한 대응이다.
        if (backend.IsSupported())
        {
            choice.backend = &backend;
            return choice;
        }
        if (!choice.passedOver.empty())
        {
            choice.passedOver += ", ";
        }
        choice.passedOver += std::string(backend.id) + " could not be created on this machine";
    }
    return choice;
}

std::unique_ptr<IGraphicsDevice> GraphicsDeviceFactory::Create(
    const std::string_view requestedId,
    std::string& selectedId)
{
    selectedId.clear();

    const GraphicsBackendDescriptor* backend = nullptr;
    std::string passedOver;
    if (IsAutomatic(requestedId))
    {
        const AutomaticBackendChoice choice =
            ChooseAutomatically(GraphicsBackendRegistry::GetBackends());
        backend = choice.backend;
        passedOver = std::move(choice.passedOver);
        if (!backend)
        {
            Diagnostics::Debug::LogError(
                "No compiled graphics backend is supported by this machine. available=",
                GraphicsBackendRegistry::DescribeAvailableIds());
            return nullptr;
        }
    }
    else
    {
        backend = GraphicsBackendRegistry::Find(requestedId);
        if (!backend)
        {
            Diagnostics::Debug::LogError(
                "The requested graphics backend is not part of this build. requested=", requestedId,
                ", available=", GraphicsBackendRegistry::DescribeAvailableIds());
            return nullptr;
        }
        if (!backend->IsSupported())
        {
            Diagnostics::Debug::LogError(
                "The requested graphics backend is not supported by the active GPU. requested=",
                requestedId);
            return nullptr;
        }
    }

    // 내려간 경우에는 그 이유를 같은 줄에 적는다. 「왜 D3D11이지?」는 이 줄을 읽는 사람이 가장
    // 먼저 묻는 것이고, 답이 다른 줄에 흩어져 있으면 로그를 뒤져야 한다.
    if (passedOver.empty())
    {
        Diagnostics::Debug::Log(
            "Graphics backend selected. requested=",
            IsAutomatic(requestedId) ? AutomaticGraphicsBackendId : requestedId,
            ", selected=", backend->id);
    }
    else
    {
        Diagnostics::Debug::Log(
            "Graphics backend selected. requested=",
            IsAutomatic(requestedId) ? AutomaticGraphicsBackendId : requestedId,
            ", selected=", backend->id, " because ", passedOver);
    }
    selectedId = backend->id;
    return backend->CreateDevice();
}

bool GraphicsDeviceFactory::IsSupported(const std::string_view requestedId)
{
    if (IsAutomatic(requestedId))
    {
        return SelectAutomatically() != nullptr;
    }
    const GraphicsBackendDescriptor* const backend = GraphicsBackendRegistry::Find(requestedId);
    return backend && backend->IsSupported();
}

}
