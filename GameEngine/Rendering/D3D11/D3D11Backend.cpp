#include "pch.h"
#include "D3D11Backend.h"

#include <d3d11.h>
#include <filesystem>
#include <iterator>
#include <memory>
#include <vector>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")

#include "D3D11GraphicsDevice.h"
#include "../Direct3D/ShaderCompiler.h"
#include "../Direct3D/ShaderPaths.h"

namespace GameEngine::Rendering::D3D11
{

namespace
{
    [[nodiscard]] bool IsHardwareSupported()
    {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL featureLevel{};
        constexpr D3D_FEATURE_LEVEL requestedLevels[] =
        {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };
        HRESULT result = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            requestedLevels, static_cast<UINT>(std::size(requestedLevels)),
            D3D11_SDK_VERSION,
            device.ReleaseAndGetAddressOf(), &featureLevel, context.ReleaseAndGetAddressOf());
        if (result == E_INVALIDARG)
        {
            // A runtime that does not know feature level 11_1 rejects the whole list.
            result = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                &requestedLevels[1], 1, D3D11_SDK_VERSION,
                device.ReleaseAndGetAddressOf(), &featureLevel, context.ReleaseAndGetAddressOf());
        }
        return SUCCEEDED(result) && featureLevel >= D3D_FEATURE_LEVEL_11_0;
    }

    [[nodiscard]] std::unique_ptr<IGraphicsDevice> CreateDevice()
    {
        return std::make_unique<D3D11GraphicsDevice>();
    }

    [[nodiscard]] std::vector<std::filesystem::path> GetRuntimeArtifacts()
    {
        return Direct3D::GetShaderRelativePaths();
    }

    [[nodiscard]] bool CompileRuntimeArtifacts(const std::filesystem::path& runtimeRootPath)
    {
        return Direct3D::CompileRuntimeArtifacts(runtimeRootPath);
    }
}

GraphicsBackendDescriptor GetGraphicsBackendDescriptor()
{
    GraphicsBackendDescriptor descriptor;
    descriptor.id = "D3D11";
    // Automatic selection reaches D3D11 when D3D12 cannot be built on this machine. It is the
    // fallback rather than the first choice, and remains selectable by name at any time.
    descriptor.automaticSelectionPriority = 50;
    descriptor.IsSupported = &IsHardwareSupported;
    descriptor.CreateDevice = &CreateDevice;
    descriptor.GetRuntimeArtifacts = &GetRuntimeArtifacts;
    descriptor.CompileRuntimeArtifacts = &CompileRuntimeArtifacts;
    return descriptor;
}

}
