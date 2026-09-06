#include "pch.h"
#include "D3D11Renderer.h"

#include "D3D11CommandList.h"
#include "D3D11ResourceResolver.h"
#include "ID3D11GraphicsDevice.h"
#include "../RenderBackendInterfaces.h"
#include "../RenderPacketEncoder.h"

#include <memory>
#include <optional>

namespace GameEngine::Rendering::D3D11
{

D3D11Renderer::D3D11Renderer()
    : mResourceResolver(std::make_unique<D3D11ResourceResolver>()),
      mCommandList(std::make_unique<D3D11CommandList>())
{
}

D3D11Renderer::~D3D11Renderer() = default;

bool D3D11Renderer::Initialize(ID3D11GraphicsDevice& graphicsDevice)
{
    return mResourceResolver->Initialize(graphicsDevice) &&
        mCommandList->Initialize(graphicsDevice);
}

void D3D11Renderer::BeginFrame()
{
    mResourceResolver->BeginFrame();
    mCommandList->BeginFrame();
}

void D3D11Renderer::BeginCapture()
{
    mResourceResolver->BeginFrame(false);
    mCommandList->BeginFrame();
}

bool D3D11Renderer::Render(const Rendering::RenderFrame& frame)
{
    // Validate before touching the context so an invalid frame leaves no backend state behind.
    const std::optional<ValidatedRenderFrame> validatedFrame = TryValidateForBackend(frame, "D3D11");
    if (!validatedFrame)
    {
        return false;
    }

    RenderPacketEncoder<D3D11ResourceResolver> encoder(*mCommandList, *mResourceResolver);
    PassDispatcher dispatcher;
    dispatcher.Dispatch(*validatedFrame, encoder, encoder);
    return true;
}

}
