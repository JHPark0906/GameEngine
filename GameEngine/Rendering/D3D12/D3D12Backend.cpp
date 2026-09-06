#include "pch.h"
#include "D3D12Backend.h"

#include "D3D12GraphicsDevice.h"
#include "../Direct3D/ShaderCompiler.h"
#include "../Direct3D/ShaderPaths.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace GameEngine::Rendering::D3D12
{

namespace
{
    [[nodiscard]] bool IsHardwareSupported()
    {
        return D3D12GraphicsDevice::IsHardwareSupported();
    }

    [[nodiscard]] std::unique_ptr<IGraphicsDevice> CreateDevice()
    {
        return std::make_unique<D3D12GraphicsDevice>();
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
    descriptor.id = "D3D12";
    // Automatic selection prefers D3D12 wherever the machine can build one, and falls to the next
    // backend when it cannot. D3D12 is the newer of the two and the one worth exercising by
    // default; a machine that cannot create it still gets D3D11 without the project saying so.
    descriptor.automaticSelectionPriority = 100;
    descriptor.IsSupported = &IsHardwareSupported;
    descriptor.CreateDevice = &CreateDevice;
    descriptor.GetRuntimeArtifacts = &GetRuntimeArtifacts;
    descriptor.CompileRuntimeArtifacts = &CompileRuntimeArtifacts;
    return descriptor;
}

}
