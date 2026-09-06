#include "pch.h"
#include "D3D12Renderer.h"

#include "D3D12CommandList.h"
#include "D3D12FrameResources.h"
#include "D3D12MeshUploader.h"
#include "D3D12ResourceResolver.h"
#include "D3D12SkinnedMeshUploader.h"
#include "D3D12TextureUploader.h"
#include "ID3D12GraphicsDevice.h"
#include "../RenderBackendInterfaces.h"
#include "../RenderPacketEncoder.h"
#include "../../Diagnostics/Debug.h"

#include <memory>
#include <optional>

namespace GameEngine::Rendering::D3D12
{

D3D12Renderer::D3D12Renderer()
    : mFrameResources(std::make_unique<D3D12FrameResources>()),
      mResourceResolver(std::make_unique<D3D12ResourceResolver>()),
      mMeshUploader(std::make_unique<D3D12MeshUploader>()),
      mSkinnedMeshUploader(std::make_unique<D3D12SkinnedMeshUploader>()),
      mTextureUploader(std::make_unique<D3D12TextureUploader>()),
      mCommandList(std::make_unique<D3D12CommandList>())
{
}

D3D12Renderer::~D3D12Renderer() = default;

bool D3D12Renderer::Initialize(ID3D12GraphicsDevice& graphicsDevice)
{
    ID3D12Device* const device = graphicsDevice.GetDevice();
    if (!device)
    {
        Diagnostics::Debug::LogError("A valid D3D12 device is required by the renderer.");
        return false;
    }
    mGraphicsDevice = &graphicsDevice;

    if (!mFrameResources->Initialize(*device))
    {
        return false;
    }
    mMeshUploader->Initialize(*device);
    mSkinnedMeshUploader->Initialize(*device);
    if (!mTextureUploader->Initialize(*device, mFrameResources->GetDescriptorAllocator()))
    {
        return false;
    }
    return mResourceResolver->Initialize(graphicsDevice) &&
        mCommandList->Initialize(
            graphicsDevice, *mFrameResources, *mMeshUploader, *mSkinnedMeshUploader,
            *mTextureUploader);
}

void D3D12Renderer::BeginFrame(const UINT frameIndex)
{
    mCurrentFrameIndex = frameIndex;
    mFrameResources->BeginFrame(frameIndex);
    mResourceResolver->BeginFrame();
    mMeshUploader->BeginFrame(frameIndex);
    mSkinnedMeshUploader->BeginFrame(frameIndex);
    mTextureUploader->BeginFrame(frameIndex);
}

void D3D12Renderer::BeginCapture(const UINT frameIndex)
{
    // 한 화면 프레임 안에 씬 뷰와 게임 뷰를 여러 번 캡처해도 캐시의 나이는 늘리지 않는다.
    // 캡처 수를 프레임 수로 세면 다른 in-flight 슬롯이 쓰는 리소스를 너무 일찍 내보낸다.
    mCurrentFrameIndex = frameIndex;
    mFrameResources->BeginFrame(frameIndex);
    mResourceResolver->BeginFrame(false);
    mMeshUploader->BeginFrame(frameIndex, false);
    mSkinnedMeshUploader->BeginFrame(frameIndex, false);
    mTextureUploader->BeginFrame(frameIndex, false);
}

bool D3D12Renderer::Render(const Rendering::RenderFrame& frame)
{
    ID3D12GraphicsCommandList* const commandList =
        mGraphicsDevice ? mGraphicsDevice->GetCommandList() : nullptr;
    if (!commandList)
    {
        Diagnostics::Debug::LogError("D3D12 cannot render without a recording command list.");
        return false;
    }

    // Validate before touching the command list so an invalid frame leaves no backend state behind.
    const std::optional<ValidatedRenderFrame> validatedFrame = TryValidateForBackend(frame, "D3D12");
    if (!validatedFrame)
    {
        return false;
    }

    if (!mFrameResources->PrepareFrame(frame)) return false;

    mFrameResources->BindFrameState(*commandList);
    mCommandList->BeginFrame(mCurrentFrameIndex, *commandList);

    RenderPacketEncoder<D3D12ResourceResolver> encoder(*mCommandList, *mResourceResolver);
    PassDispatcher dispatcher;
    dispatcher.Dispatch(*validatedFrame, encoder, encoder);
    return true;
}

}
