#include "pch.h"
#include "D3D11GraphicsDevice.h"

#include <cstddef>
#include <cstring>
#include <DirectXColors.h>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include "D3D11Renderer.h"
#include "../Direct3D/DeviceRemoval.h"
#include "../Direct3D/DxgiWindowAssociation.h"
#include "../RenderColorPolicy.h"
#include "../../Platform/Win32/Win32Diagnostics.h"

namespace GameEngine::Rendering::D3D11
{

using Platform::Win32::LogHResult;

D3D11GraphicsDevice::D3D11GraphicsDevice() = default;
D3D11GraphicsDevice::~D3D11GraphicsDevice() = default;

bool D3D11GraphicsDevice::Initialize(const Platform::NativeSurface& surface)
{
    // 표면 없이 초기화하면 headless다: swap chain이 없고, BeginFrame과 EndFrame은 오류이며,
    // 프레임은 RenderToImage로만 나온다. 캡처만 소비하는 곳 — 게임 뷰를 이미지로 받는 에디터,
    // 백엔드를 비교하는 테스트 — 은 present할 창 자체가 필요 없다.
    if (surface.kind == Platform::NativeSurfaceKind::None)
    {
        if (!CreateDeviceWithoutSwapChain())
        {
            return false;
        }
        mRenderer = std::make_unique<D3D11Renderer>();
        if (!mRenderer->Initialize(*this))
        {
            Diagnostics::Debug::LogError("Failed to initialize the D3D11 renderer.");
            return false;
        }
        return true;
    }

    if (surface.kind != Platform::NativeSurfaceKind::Win32)
    {
        Diagnostics::Debug::LogError("D3D11 can only present to a Win32 surface.");
        return false;
    }
    const HWND windowHandle = static_cast<HWND>(surface.handle);
    if (!windowHandle)
    {
        Diagnostics::Debug::LogError("A valid window handle is required to initialize D3D11.");
        return false;
    }
    mWindowHandle = windowHandle;

    if (!CreateDeviceAndSwapChain(windowHandle))
    {
        return false;
    }

    if (!CreateRenderTargetView())
    {
        return false;
    }

    RECT clientRectangle = {};
    GetClientRect(windowHandle, &clientRectangle);
    const UINT width = static_cast<UINT>(clientRectangle.right - clientRectangle.left);
    const UINT height = static_cast<UINT>(clientRectangle.bottom - clientRectangle.top);
    if (!CreateDepthStencilView(width, height))
    {
        return false;
    }
    SetViewport(width, height);
    mRenderer = std::make_unique<D3D11Renderer>();
    if (!mRenderer->Initialize(*this))
    {
        Diagnostics::Debug::LogError("Failed to initialize the D3D11 renderer.");
        return false;
    }
    return true;
}

bool D3D11GraphicsDevice::BeginFrame()
{
    if (!mSwapChain)
    {
        Diagnostics::Debug::LogError(
            "A headless D3D11 device has no frame to begin; use RenderToImage.");
        return false;
    }
    if (!ResizeIfNeeded())
    {
        return false;
    }
    mRenderer->BeginFrame();

    mDeviceContext->OMSetRenderTargets(
        1, mRenderTargetView.GetAddressOf(), mDepthStencilView.Get());
    mDeviceContext->RSSetViewports(1, &mViewport);
    mFrameInProgress = true;
    return true;
}

Rendering::RenderTargetSize D3D11GraphicsDevice::GetRenderTargetSize() const
{
    return {
        static_cast<unsigned int>(mViewport.Width),
        static_cast<unsigned int>(mViewport.Height)
    };
}

Rendering::GraphicsDeviceCapabilities D3D11GraphicsDevice::GetCapabilities() const
{
    // Taken from the API header rather than compared against a shared constant, so the value cannot
    // drift from what this device actually accepts.
    return { D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION };
}

bool D3D11GraphicsDevice::Render(const Rendering::RenderFrame& frame)
{
    // Clearing belongs to rendering the frame, not to starting it: the background colour is camera
    // state the frame carries, and a rejected frame still leaves a cleared target to present.
    const Math::Color clearColor = Rendering::GetLinearClearColor(frame);
    const float clearValues[] = { clearColor.r, clearColor.g, clearColor.b, clearColor.a };
    mDeviceContext->ClearRenderTargetView(mRenderTargetView.Get(), clearValues);
    mDeviceContext->ClearDepthStencilView(
        mDepthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    return mRenderer->Render(frame);
}

void D3D11GraphicsDevice::DrainDebugMessages()
{
    if (!mInfoQueue)
    {
        return;
    }
    const UINT64 messageCount = mInfoQueue->GetNumStoredMessages();
    std::vector<std::byte> buffer;
    for (UINT64 index = 0; index < messageCount; ++index)
    {
        SIZE_T messageLength = 0;
        if (FAILED(mInfoQueue->GetMessage(index, nullptr, &messageLength)) || messageLength == 0)
        {
            continue;
        }
        buffer.resize(messageLength);
        auto* const message = reinterpret_cast<D3D11_MESSAGE*>(buffer.data());
        if (FAILED(mInfoQueue->GetMessage(index, message, &messageLength)))
        {
            continue;
        }
        const std::string description(message->pDescription, message->DescriptionByteLength);
        if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
        {
            Diagnostics::Debug::LogError("D3D11 debug layer: ", description);
        }
        else
        {
            Diagnostics::Debug::Log("D3D11 debug layer: ", description);
        }
    }
    mInfoQueue->ClearStoredMessages();
}

bool D3D11GraphicsDevice::EndFrame()
{
    mFrameInProgress = false;
    DrainDebugMessages();

    if (!mSwapChain)
    {
        Diagnostics::Debug::LogError(
            "A headless D3D11 device has nothing to present; use RenderToImage.");
        return false;
    }
    const HRESULT result = mSwapChain->Present(1, 0);
    if (FAILED(result))
    {
        LogHResult("D3D11 Present", result);
        // 장치를 잃었는지는 present의 코드만으로는 다 말해지지 않는다. 장치에게 사유를 물어
        // 실행 로그가 "왜 죽었는지"를 담게 한다.
        static_cast<void>(Direct3D::ReportDeviceRemoval(
            "Direct3D 11", result, mDevice ? mDevice->GetDeviceRemovedReason() : S_OK));
        return false;
    }

    return true;
}

D3D11GraphicsDevice::CaptureTarget* D3D11GraphicsDevice::EnsureCaptureTarget(
    const unsigned int channel, const UINT width, const UINT height)
{
    CaptureTarget& target = mCaptureTargets[channel];
    if (target.width == width && target.height == height && target.colorView &&
        target.stagingTextures[0] && target.stagingTextures[1])
    {
        return &target;
    }

    target = {};

    D3D11_TEXTURE2D_DESC colorDescription = {};
    colorDescription.Width = width;
    colorDescription.Height = height;
    colorDescription.MipLevels = 1;
    colorDescription.ArraySize = 1;
    // sRGB 타깃: 셰이더는 선형 값을 쓰고 뷰가 인코딩한다. 캡처된 바이트는 그래서 sRGB다.
    colorDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    colorDescription.SampleDesc.Count = 1;
    colorDescription.Usage = D3D11_USAGE_DEFAULT;
    colorDescription.BindFlags = D3D11_BIND_RENDER_TARGET;
    HRESULT result = mDevice->CreateTexture2D(
        &colorDescription, nullptr, target.colorTexture.ReleaseAndGetAddressOf());
    if (SUCCEEDED(result))
    {
        result = mDevice->CreateRenderTargetView(
            target.colorTexture.Get(), nullptr, target.colorView.ReleaseAndGetAddressOf());
    }
    if (FAILED(result))
    {
        LogHResult("Creating the D3D11 capture target", result);
        target = {};
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC depthDescription = colorDescription;
    depthDescription.Format = DXGI_FORMAT_D32_FLOAT;
    depthDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    result = mDevice->CreateTexture2D(
        &depthDescription, nullptr, target.depthTexture.ReleaseAndGetAddressOf());
    if (SUCCEEDED(result))
    {
        result = mDevice->CreateDepthStencilView(
            target.depthTexture.Get(), nullptr, target.depthView.ReleaseAndGetAddressOf());
    }
    if (FAILED(result))
    {
        LogHResult("Creating the D3D11 capture depth buffer", result);
        target = {};
        return nullptr;
    }

    // The GPU cannot be read from directly, so the finished target is copied into a texture the CPU
    // may map. It is a separate resource because a texture that is both a render target and CPU
    // readable is not a thing D3D11 offers. Two of them, so a deferred capture can copy into one
    // while the other — a frame older — is mapped.
    D3D11_TEXTURE2D_DESC stagingDescription = colorDescription;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (Microsoft::WRL::ComPtr<ID3D11Texture2D>& staging : target.stagingTextures)
    {
        result = mDevice->CreateTexture2D(
            &stagingDescription, nullptr, staging.ReleaseAndGetAddressOf());
        if (FAILED(result))
        {
            LogHResult("Creating the D3D11 capture staging texture", result);
            target = {};
            return nullptr;
        }
    }

    target.width = width;
    target.height = height;
    return &target;
}

bool D3D11GraphicsDevice::RenderToImage(
    const Rendering::RenderFrame& frame, Rendering::CapturedImage& image,
    const Rendering::IGraphicsDevice::CaptureRequest& request)
{
    image = {};

    if (mFrameInProgress)
    {
        Diagnostics::Debug::LogError(
            "A capture cannot be taken between BeginFrame and EndFrame: it rebinds the render "
            "target, and the frame in progress would draw somewhere it is not presented from.");
        return false;
    }

    const Rendering::RenderTargetSize size = frame.GetRenderTargetSize();
    if (size.width == 0 || size.height == 0)
    {
        Diagnostics::Debug::LogError("A frame with no size cannot be rendered to an image.");
        return false;
    }
    CaptureTarget* const target = EnsureCaptureTarget(request.channel, size.width, size.height);
    if (!target)
    {
        return false;
    }

    // Whatever happens below, the swap chain's target is bound again on the way out, so a failed
    // capture never leaves the context pointing at the capture target.
    const auto restoreWindowTarget = [this]()
    {
        if (mRenderTargetView)
        {
            mDeviceContext->OMSetRenderTargets(
                1, mRenderTargetView.GetAddressOf(), mDepthStencilView.Get());
            mDeviceContext->RSSetViewports(1, &mViewport);
        }
    };

    if (mSwapChain) mRenderer->BeginCapture();
    else mRenderer->BeginFrame(); // A headless capture is its own cache frame.
    mDeviceContext->OMSetRenderTargets(
        1, target->colorView.GetAddressOf(), target->depthView.Get());
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(size.width);
    viewport.Height = static_cast<float>(size.height);
    viewport.MaxDepth = 1.0f;
    mDeviceContext->RSSetViewports(1, &viewport);

    const Math::Color clearColor = Rendering::GetLinearClearColor(frame);
    const float clearValues[] = { clearColor.r, clearColor.g, clearColor.b, clearColor.a };
    mDeviceContext->ClearRenderTargetView(target->colorView.Get(), clearValues);
    mDeviceContext->ClearDepthStencilView(target->depthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    // A rejected frame still produces an image: the cleared target is the honest answer, and a
    // caller comparing backends wants to see it rather than an error with no pixels.
    const bool rendered = mRenderer->Render(frame);

    const unsigned int writeIndex = target->writeIndex;
    mDeviceContext->CopyResource(
        target->stagingTextures[writeIndex].Get(), target->colorTexture.Get());
    restoreWindowTarget();

    // Immediate: map what was just copied, waiting for the GPU. Deferred: map the other staging
    // texture, whose copy is a frame old, and leave this one for next time.
    unsigned int readIndex = writeIndex;
    if (request.deferred)
    {
        target->writeIndex ^= 1u;
        if (!target->hasPendingCopy)
        {
            target->hasPendingCopy = true;
            return false;
        }
        readIndex = writeIndex ^ 1u;
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT mapResult = mDeviceContext->Map(
        target->stagingTextures[readIndex].Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(mapResult))
    {
        LogHResult("Mapping the D3D11 capture staging texture", mapResult);
        return false;
    }

    image.width = size.width;
    image.height = size.height;
    image.pixels.resize(image.GetByteSize());
    const std::size_t rowBytes =
        static_cast<std::size_t>(size.width) * Rendering::CapturedImage::BytesPerPixel;
    for (UINT row = 0; row < size.height; ++row)
    {
        std::memcpy(
            image.pixels.data() + static_cast<std::size_t>(row) * rowBytes,
            static_cast<const std::byte*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch,
            rowBytes);
    }
    mDeviceContext->Unmap(target->stagingTextures[readIndex].Get(), 0);

    return rendered;
}

bool D3D11GraphicsDevice::CreateDeviceWithoutSwapChain()
{
    UINT createDeviceFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel = {};
    const HRESULT result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        mDevice.ReleaseAndGetAddressOf(),
        &featureLevel,
        mDeviceContext.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("D3D11CreateDevice", result);
        return false;
    }
    if (featureLevel < D3D_FEATURE_LEVEL_11_0)
    {
        Diagnostics::Debug::LogError("Direct3D feature level 11 is required.");
        return false;
    }

#if defined(DEBUG) || defined(_DEBUG)
    if (FAILED(mDevice.As(&mInfoQueue)))
    {
        Diagnostics::Debug::LogWarning(
            "The D3D11 debug layer is unavailable, so validation messages will not be logged.");
    }
#endif
    return true;
}

bool D3D11GraphicsDevice::CreateDeviceAndSwapChain(const HWND windowHandle)
{
    UINT createDeviceFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    RECT clientRectangle = {};
    GetClientRect(windowHandle, &clientRectangle);

    // The flip model is the presentation path Windows 10 and later support; the legacy bitblt model
    // copies through the desktop compositor every frame. D3D12 already uses FLIP_DISCARD, so this
    // also keeps the two backends on the same presentation behaviour.
    DXGI_SWAP_CHAIN_DESC descriptor = {};
    descriptor.BufferDesc.Width = static_cast<UINT>(clientRectangle.right - clientRectangle.left);
    descriptor.BufferDesc.Height = static_cast<UINT>(clientRectangle.bottom - clientRectangle.top);
    descriptor.BufferDesc.RefreshRate.Numerator = 60;
    descriptor.BufferDesc.RefreshRate.Denominator = 1;
    descriptor.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    descriptor.SampleDesc.Count = 1;
    descriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    descriptor.BufferCount = BufferCount;
    descriptor.OutputWindow = windowHandle;
    descriptor.Windowed = true;
    descriptor.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL featureLevel = {};
    const HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &descriptor,
        mSwapChain.ReleaseAndGetAddressOf(),
        mDevice.ReleaseAndGetAddressOf(),
        &featureLevel,
        mDeviceContext.ReleaseAndGetAddressOf());

    if (FAILED(result))
    {
        LogHResult("D3D11CreateDeviceAndSwapChain", result);
        return false;
    }

    if (featureLevel < D3D_FEATURE_LEVEL_11_0)
    {
        Diagnostics::Debug::LogError("Direct3D feature level 11 is required.");
        return false;
    }

    // 두 백엔드가 같은 창 규약을 갖는다. 한쪽만 DXGI의 메시지 큐 감시를 켜 두면, 백엔드를 바꾸는
    // 것만으로 재현되지 않는 교착이 생긴다.
    if (!Direct3D::DisableDxgiWindowMonitoring(mSwapChain.Get(), windowHandle))
    {
        return false;
    }

#if defined(DEBUG) || defined(_DEBUG)
    if (FAILED(mDevice.As(&mInfoQueue)))
    {
        Diagnostics::Debug::LogWarning(
            "The D3D11 debug layer is unavailable, so validation messages will not be logged.");
    }
#endif
    return true;
}

bool D3D11GraphicsDevice::CreateRenderTargetView()
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT result = mSwapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        Diagnostics::Debug::LogError("Failed to get the swap-chain back buffer.");
        return false;
    }

    // flip 모델 스왑 체인은 sRGB 버퍼를 허용하지 않으므로 버퍼는 UNORM이고 뷰가 sRGB다. 쓰는
    // 순간 인코딩되는 것은 같다.
    D3D11_RENDER_TARGET_VIEW_DESC viewDescription = {};
    viewDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    viewDescription.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    result = mDevice->CreateRenderTargetView(
        backBuffer.Get(),
        &viewDescription,
        mRenderTargetView.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        Diagnostics::Debug::LogError("Failed to create the render-target view.");
        return false;
    }

    return true;
}

bool D3D11GraphicsDevice::CreateDepthStencilView(const UINT width, const UINT height)
{
    D3D11_TEXTURE2D_DESC textureDescriptor = {};
    textureDescriptor.Width = width;
    textureDescriptor.Height = height;
    textureDescriptor.MipLevels = 1;
    textureDescriptor.ArraySize = 1;
    textureDescriptor.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    textureDescriptor.SampleDesc.Count = 1;
    textureDescriptor.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    HRESULT result = mDevice->CreateTexture2D(
        &textureDescriptor, nullptr, mDepthStencilTexture.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Creating the D3D11 depth-stencil texture", result);
        return false;
    }

    result = mDevice->CreateDepthStencilView(
        mDepthStencilTexture.Get(), nullptr, mDepthStencilView.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Creating the D3D11 depth-stencil view", result);
        return false;
    }
    return true;
}

bool D3D11GraphicsDevice::ResizeIfNeeded()
{
    RECT clientRectangle = {};
    if (!GetClientRect(mWindowHandle, &clientRectangle))
    {
        Diagnostics::Debug::LogError("Failed to query the D3D11 window client size.");
        return false;
    }

    const UINT width = static_cast<UINT>(clientRectangle.right - clientRectangle.left);
    const UINT height = static_cast<UINT>(clientRectangle.bottom - clientRectangle.top);
    if (width == 0 || height == 0 ||
        (mViewport.Width == static_cast<float>(width) &&
         mViewport.Height == static_cast<float>(height)))
    {
        return true;
    }

    mDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    mRenderTargetView.Reset();
    mDepthStencilView.Reset();
    mDepthStencilTexture.Reset();
    const HRESULT result = mSwapChain->ResizeBuffers(
        0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(result))
    {
        LogHResult("Resizing the D3D11 swap-chain buffers", result);
        return false;
    }

    if (!CreateRenderTargetView())
    {
        return false;
    }
    if (!CreateDepthStencilView(width, height))
    {
        return false;
    }

    SetViewport(width, height);
    return true;
}

void D3D11GraphicsDevice::SetViewport(const UINT width, const UINT height)
{
    mViewport.TopLeftX = 0.0f;
    mViewport.TopLeftY = 0.0f;
    mViewport.Width = static_cast<float>(width);
    mViewport.Height = static_cast<float>(height);
    mViewport.MinDepth = 0.0f;
    mViewport.MaxDepth = 1.0f;
}

}
