#include "pch.h"
#include "D3D12GraphicsDevice.h"

#include <windows.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#include "D3D12Renderer.h"
#include "D3D12PipelineDescriptions.h"
#include "../Direct3D/DeviceRemoval.h"
#include "../Direct3D/DxgiWindowAssociation.h"
#include "../RenderColorPolicy.h"
#include "../../Platform/Win32/Win32Diagnostics.h"

namespace GameEngine::Rendering::D3D12
{

using Platform::Win32::LogHResult;

namespace
{
    /// <summary>
    /// 펜스 대기의 기한이다. 정상적인 프레임 작업은 이 근처에도 오지 않는다 — Windows가
    /// 응답 없는 GPU를 리셋하는 기한(TDR)이 2초다. 여기서의 목적은 늦은 프레임을 잡는 것이
    /// 아니라, 다시는 오지 않을 신호를 영원히 기다리지 않는 것이다.
    /// </summary>
    constexpr DWORD FenceWaitTimeoutMilliseconds = 5000;
}

D3D12GraphicsDevice::D3D12GraphicsDevice() = default;

D3D12GraphicsDevice::~D3D12GraphicsDevice()
{
    (void)WaitForGpu();
    if (mFenceEvent)
    {
        CloseHandle(mFenceEvent);
    }
}

bool D3D12GraphicsDevice::IsHardwareSupported()
{
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()))))
    {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0;
         factory->EnumAdapters1(index, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND;
         ++index)
    {
        DXGI_ADAPTER_DESC1 descriptor = {};
        adapter->GetDesc1(&descriptor);
        if ((descriptor.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
            SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
        {
            return true;
        }
    }
    return false;
}

bool D3D12GraphicsDevice::Initialize(const Platform::NativeSurface& surface)
{
    // 표면 없이 초기화하면 headless다: swap chain이 없고, BeginFrame과 EndFrame은 오류이며,
    // 프레임은 RenderToImage로만 나온다. 장치, 명령 큐, 펜스는 캡처에도 필요하므로 만든다.
    if (surface.kind == Platform::NativeSurfaceKind::None)
    {
        if (!CreateDevice() || !CreateCommandObjects())
        {
            return false;
        }
        mRenderer = std::make_unique<D3D12Renderer>();
        if (!mRenderer->Initialize(*this))
        {
            Diagnostics::Debug::LogError("Failed to initialize the D3D12 renderer.");
            return false;
        }
        return true;
    }

    if (surface.kind != Platform::NativeSurfaceKind::Win32)
    {
        Diagnostics::Debug::LogError("D3D12 can only present to a Win32 surface.");
        return false;
    }
    const HWND windowHandle = static_cast<HWND>(surface.handle);
    if (!windowHandle)
    {
        Diagnostics::Debug::LogError("A valid window handle is required to initialize D3D12.");
        return false;
    }

    RECT clientRectangle = {};
    if (!GetClientRect(windowHandle, &clientRectangle))
    {
        Diagnostics::Debug::LogError("Failed to query the D3D12 window client size.");
        return false;
    }
    const UINT width = static_cast<UINT>(clientRectangle.right - clientRectangle.left);
    const UINT height = static_cast<UINT>(clientRectangle.bottom - clientRectangle.top);
    if (width == 0 || height == 0)
    {
        Diagnostics::Debug::LogError("D3D12 cannot initialize a zero-sized render target.");
        return false;
    }

    mWindowHandle = windowHandle;
    if (!CreateDevice() ||
        !CreateCommandObjects() ||
        !CreateSwapChain(width, height) ||
        !CreateRenderTargetViews() ||
        !CreateDepthStencilView(width, height))
    {
        return false;
    }
    // The viewport must be current before the renderer initializes: its passes read it through
    // ID3D12GraphicsDevice rather than receiving it through a separate notification.
    SetViewport(width, height);
    mRenderer = std::make_unique<D3D12Renderer>();
    if (!mRenderer->Initialize(*this))
    {
        Diagnostics::Debug::LogError("Failed to initialize the D3D12 renderer.");
        return false;
    }
    return true;
}

bool D3D12GraphicsDevice::BeginFrame()
{
    if (mSubmissionFailed)
    {
        Diagnostics::Debug::LogError("D3D12 cannot begin a frame after a command submission failure.");
        return false;
    }
    if (!mSwapChain)
    {
        Diagnostics::Debug::LogError(
            "A headless D3D12 device has no frame to begin; use RenderToImage.");
        return false;
    }
    if (!ResizeIfNeeded())
    {
        return false;
    }

    // Wait for this slot alone rather than for the whole GPU. The frame submitted from the other
    // slot may still be executing, which is the point: the CPU builds the next frame while it does.
    if (!WaitForFrameSlot(mCurrentFrameIndex))
    {
        return false;
    }

    const HRESULT allocatorResult = mCommandAllocators[mCurrentFrameIndex]->Reset();
    if (FAILED(allocatorResult))
    {
        LogHResult("Failed to reset the D3D12 command allocator", allocatorResult);
        return false;
    }
    const HRESULT listResult = mCommandList->Reset(
        mCommandAllocators[mCurrentFrameIndex].Get(), nullptr);
    if (FAILED(listResult))
    {
        LogHResult("Failed to reset the D3D12 command list", listResult);
        return false;
    }
    mRenderer->BeginFrame(mCurrentFrameIndex);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = mRenderTargets[mCurrentFrameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    mCommandList->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE renderTargetHandle =
        mRenderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
    renderTargetHandle.ptr +=
        static_cast<SIZE_T>(mCurrentFrameIndex) * mRenderTargetViewDescriptorSize;
    const D3D12_CPU_DESCRIPTOR_HANDLE depthStencilHandle =
        mDepthStencilViewHeap->GetCPUDescriptorHandleForHeapStart();
    mCommandList->OMSetRenderTargets(1, &renderTargetHandle, FALSE, &depthStencilHandle);
    mCommandList->RSSetViewports(1, &mViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRectangle);
    mFrameInProgress = true;
    return true;
}

Rendering::RenderTargetSize D3D12GraphicsDevice::GetRenderTargetSize() const
{
    return {
        static_cast<unsigned int>(mViewport.Width),
        static_cast<unsigned int>(mViewport.Height)
    };
}

Rendering::GraphicsDeviceCapabilities D3D12GraphicsDevice::GetCapabilities() const
{
    // Taken from the API header rather than compared against a shared constant, so the value cannot
    // drift from what this device actually accepts.
    return { D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION };
}

bool D3D12GraphicsDevice::Render(const Rendering::RenderFrame& frame)
{
    // Clearing belongs to rendering the frame, not to starting it: the background colour is camera
    // state the frame carries, and a rejected frame still leaves a cleared target to present.
    D3D12_CPU_DESCRIPTOR_HANDLE renderTargetHandle =
        mRenderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
    renderTargetHandle.ptr +=
        static_cast<SIZE_T>(mCurrentFrameIndex) * mRenderTargetViewDescriptorSize;
    const Math::Color clearColor = Rendering::GetLinearClearColor(frame);
    const float clearValues[] = { clearColor.r, clearColor.g, clearColor.b, clearColor.a };
    mCommandList->ClearRenderTargetView(renderTargetHandle, clearValues, 0, nullptr);
    mCommandList->ClearDepthStencilView(
        mDepthStencilViewHeap->GetCPUDescriptorHandleForHeapStart(),
        D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    return mRenderer->Render(frame);
}

void D3D12GraphicsDevice::DrainDebugMessages()
{
    // Forward Direct3D validation messages into the engine log so usage errors are visible
    // when no debugger is attached.
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
        auto* const message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
        if (FAILED(mInfoQueue->GetMessage(index, message, &messageLength)))
        {
            continue;
        }
        const std::string description(message->pDescription, message->DescriptionByteLength);
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
        {
            Diagnostics::Debug::LogError("D3D12 debug layer: ", description);
        }
        else
        {
            Diagnostics::Debug::Log("D3D12 debug layer: ", description);
        }
    }
    mInfoQueue->ClearStoredMessages();
}

bool D3D12GraphicsDevice::EndFrame()
{
    mFrameInProgress = false;
    DrainDebugMessages();

    if (!mSwapChain)
    {
        Diagnostics::Debug::LogError(
            "A headless D3D12 device has nothing to present; use RenderToImage.");
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = mRenderTargets[mCurrentFrameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    mCommandList->ResourceBarrier(1, &barrier);

    const HRESULT closeResult = mCommandList->Close();
    if (FAILED(closeResult))
    {
        mSubmissionFailed = true;
        LogHResult("Failed to close the D3D12 command list", closeResult);
        static_cast<void>(ReportDeviceRemovalIfLost(closeResult));
        return false;
    }
    ID3D12CommandList* commandLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, commandLists);

    const HRESULT presentResult = mSwapChain->Present(1, 0);
    if (FAILED(presentResult))
    {
        mSubmissionFailed = true;
        LogHResult("D3D12 Present", presentResult);
        // present의 코드는 "장치가 없다"까지만 말한다. 사유는 장치에게 물어야 나온다.
        static_cast<void>(ReportDeviceRemovalIfLost(presentResult));
        return false;
    }

    // Record what this slot's work will reach, instead of waiting for it here. Nothing this frame
    // used may be reused or released until that value is reached, which is what BeginFrame waits on
    // and what the caches retain their entries for.
    const UINT64 fenceValue = ++mFenceValue;
    const HRESULT signalResult = mCommandQueue->Signal(mFence.Get(), fenceValue);
    if (FAILED(signalResult))
    {
        mSubmissionFailed = true;
        LogHResult("Signalling the D3D12 frame fence", signalResult);
        static_cast<void>(ReportDeviceRemovalIfLost(signalResult));
        return false;
    }
    mFrameFenceValues[mCurrentFrameIndex] = fenceValue;

    mCurrentFrameIndex = mSwapChain->GetCurrentBackBufferIndex();
    return true;
}

D3D12GraphicsDevice::CaptureTarget* D3D12GraphicsDevice::EnsureCaptureTarget(
    const unsigned int channel, const UINT width, const UINT height)
{
    CaptureTarget& target = mCaptureTargets[channel];
    if (target.width == width && target.height == height && target.parities[0].readbackBuffer)
    {
        return &target;
    }

    // Anything the previous target held may still be referenced by work in flight — a capture
    // whose fence has not been checked yet — so the GPU is drained before it is released.
    if (target.colorTexture && !WaitForGpu())
    {
        return nullptr;
    }
    target = {};

    D3D12_DESCRIPTOR_HEAP_DESC renderTargetHeapDescriptor = {};
    renderTargetHeapDescriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    renderTargetHeapDescriptor.NumDescriptors = 1;
    HRESULT result = mDevice->CreateDescriptorHeap(
        &renderTargetHeapDescriptor,
        IID_PPV_ARGS(target.renderTargetViewHeap.ReleaseAndGetAddressOf()));
    if (SUCCEEDED(result))
    {
        D3D12_DESCRIPTOR_HEAP_DESC depthStencilHeapDescriptor = {};
        depthStencilHeapDescriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        depthStencilHeapDescriptor.NumDescriptors = 1;
        result = mDevice->CreateDescriptorHeap(
            &depthStencilHeapDescriptor,
            IID_PPV_ARGS(target.depthStencilViewHeap.ReleaseAndGetAddressOf()));
    }
    for (CaptureTarget::Parity& parity : target.parities)
    {
        if (SUCCEEDED(result))
        {
            result = mDevice->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(parity.commandAllocator.ReleaseAndGetAddressOf()));
        }
    }
    if (FAILED(result))
    {
        LogHResult("Creating the D3D12 capture heaps", result);
        target = {};
        return nullptr;
    }

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC colorDescriptor = {};
    colorDescriptor.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDescriptor.Width = width;
    colorDescriptor.Height = height;
    colorDescriptor.DepthOrArraySize = 1;
    colorDescriptor.MipLevels = 1;
    colorDescriptor.Format = RenderTargetFormat;
    colorDescriptor.SampleDesc.Count = 1;
    colorDescriptor.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    colorDescriptor.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    result = mDevice->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &colorDescriptor,
        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
        IID_PPV_ARGS(target.colorTexture.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating the D3D12 capture target", result);
        target = {};
        return nullptr;
    }
    mDevice->CreateRenderTargetView(
        target.colorTexture.Get(), nullptr,
        target.renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart());

    // The same format the window depth buffer uses: a pipeline state names the depth format it
    // draws into, so a capture with a different one would draw nothing the window draws.
    D3D12_RESOURCE_DESC depthDescriptor = colorDescriptor;
    depthDescriptor.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDescriptor.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE depthClearValue = {};
    depthClearValue.Format = depthDescriptor.Format;
    depthClearValue.DepthStencil.Depth = 1.0f;
    result = mDevice->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &depthDescriptor,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue,
        IID_PPV_ARGS(target.depthTexture.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating the D3D12 capture depth buffer", result);
        target = {};
        return nullptr;
    }
    mDevice->CreateDepthStencilView(
        target.depthTexture.Get(), nullptr,
        target.depthStencilViewHeap->GetCPUDescriptorHandleForHeapStart());

    // A copy out of a texture lands with its rows padded to a 256-byte multiple, so the buffer is
    // sized by what the device says the copy will produce rather than by the row length of the
    // image.
    UINT64 readbackSize = 0;
    mDevice->GetCopyableFootprints(
        &colorDescriptor, 0, 1, 0, &target.footprint, &target.rows, nullptr, &readbackSize);

    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDescriptor = {};
    readbackDescriptor.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDescriptor.Width = readbackSize;
    readbackDescriptor.Height = 1;
    readbackDescriptor.DepthOrArraySize = 1;
    readbackDescriptor.MipLevels = 1;
    readbackDescriptor.Format = DXGI_FORMAT_UNKNOWN;
    readbackDescriptor.SampleDesc.Count = 1;
    readbackDescriptor.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (CaptureTarget::Parity& parity : target.parities)
    {
        result = mDevice->CreateCommittedResource(
            &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDescriptor,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(parity.readbackBuffer.ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            LogHResult("Creating the D3D12 capture readback buffer", result);
            target = {};
            return nullptr;
        }
    }

    target.readbackSize = readbackSize;
    target.width = width;
    target.height = height;
    return &target;
}

bool D3D12GraphicsDevice::ReportDeviceRemovalIfLost(const HRESULT operationResult) const
{
    return Direct3D::ReportDeviceRemoval(
        "Direct3D 12", operationResult, mDevice ? mDevice->GetDeviceRemovedReason() : S_OK);
}

bool D3D12GraphicsDevice::WaitForFenceEvent(const char* const operation)
{
    const DWORD waitResult = WaitForSingleObject(mFenceEvent, FenceWaitTimeoutMilliseconds);
    if (waitResult == WAIT_OBJECT_0)
    {
        return true;
    }
    if (waitResult == WAIT_TIMEOUT)
    {
        Diagnostics::Debug::LogError(
            operation, " did not complete within ", FenceWaitTimeoutMilliseconds,
            " ms; the GPU is not making progress.");
    }
    else
    {
        LogHResult(operation, HRESULT_FROM_WIN32(GetLastError()));
    }
    // 신호가 오지 않는 가장 흔한 이유가 장치 손실이다. 사유를 물어 두면 행의 원인이 로그에
    // 남는다.
    static_cast<void>(ReportDeviceRemovalIfLost(S_OK));
    return false;
}

bool D3D12GraphicsDevice::WaitForFenceValue(const UINT64 fenceValue)
{
    if (!mFence || !mFenceEvent || fenceValue == 0 || mFence->GetCompletedValue() >= fenceValue)
    {
        return true;
    }
    const HRESULT result = mFence->SetEventOnCompletion(fenceValue, mFenceEvent);
    if (FAILED(result))
    {
        LogHResult("Waiting for a D3D12 capture fence", result);
        static_cast<void>(ReportDeviceRemovalIfLost(result));
        return false;
    }
    return WaitForFenceEvent("Waiting for a D3D12 capture fence");
}

bool D3D12GraphicsDevice::ReadCapture(
    const CaptureTarget& target, const unsigned int parity, Rendering::CapturedImage& image) const
{
    const CaptureTarget::Parity& side = target.parities[parity];
    const D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(target.readbackSize) };
    void* mapped = nullptr;
    const HRESULT mapResult = side.readbackBuffer->Map(0, &readRange, &mapped);
    if (FAILED(mapResult))
    {
        LogHResult("Mapping the D3D12 capture readback buffer", mapResult);
        return false;
    }

    image.width = target.width;
    image.height = target.height;
    image.pixels.resize(image.GetByteSize());
    const std::size_t rowBytes =
        static_cast<std::size_t>(target.width) * Rendering::CapturedImage::BytesPerPixel;
    const auto rowPitch = static_cast<std::size_t>(target.footprint.Footprint.RowPitch);
    for (UINT row = 0; row < target.height; ++row)
    {
        std::memcpy(
            image.pixels.data() + static_cast<std::size_t>(row) * rowBytes,
            static_cast<const std::byte*>(mapped) + static_cast<std::size_t>(row) * rowPitch,
            rowBytes);
    }
    const D3D12_RANGE writtenRange = { 0, 0 };
    side.readbackBuffer->Unmap(0, &writtenRange);
    return true;
}

bool D3D12GraphicsDevice::RenderToImage(
    const Rendering::RenderFrame& frame, Rendering::CapturedImage& image,
    const Rendering::IGraphicsDevice::CaptureRequest& request)
{
    image = {};
    bool advanceHeadlessFrame = !mSwapChain;
    SubmittedCapture capture;
    if (!SubmitCapture(frame, request.channel, advanceHeadlessFrame, capture))
    {
        return false;
    }

    // Immediate reads this submission; deferred reads the preceding parity of the same channel.
    unsigned int readParity = capture.parity;
    if (request.deferred)
    {
        readParity ^= 1u;
        if (capture.target->parities[readParity].fenceValue == 0)
        {
            return false;
        }
    }
    if (!WaitForFenceValue(capture.target->parities[readParity].fenceValue))
    {
        return false;
    }
    return ReadCapture(*capture.target, readParity, image) && capture.rendered;
}

void D3D12GraphicsDevice::RenderToImages(const std::span<CaptureBatchItem> items)
{
    // Repeated channels can replace a target or wrap its two readback slots before collection.
    // Aliased outputs must retain the last sequential call's image, including on failure.
    if (items.size() > MaxDeferredCaptureChannels)
    {
        IGraphicsDevice::RenderToImages(items);
        return;
    }
    std::array<bool, MaxDeferredCaptureChannels> channels{};
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const CaptureBatchItem& item = items[index];
        if (item.channel < channels.size())
        {
            if (channels[item.channel])
            {
                IGraphicsDevice::RenderToImages(items);
                return;
            }
            channels[item.channel] = true;
        }
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (&items[previous].image == &item.image)
            {
                IGraphicsDevice::RenderToImages(items);
                return;
            }
        }
    }

    std::array<SubmittedCapture, MaxDeferredCaptureChannels> pending{};
    bool advanceHeadlessFrame = !mSwapChain;
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        CaptureBatchItem& item = items[index];
        item.succeeded = false;
        item.image = {};
        static_cast<void>(SubmitCapture(
            item.frame, item.channel, advanceHeadlessFrame, pending[index]));
    }
    // All captures are submitted before any current image is read. Reading an earlier image
    // can overlap later GPU work, while every result still belongs to this synchronous call.
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const SubmittedCapture& capture = pending[index];
        if (capture.target && WaitForFenceValue(capture.target->parities[capture.parity].fenceValue))
        {
            items[index].succeeded =
                ReadCapture(*capture.target, capture.parity, items[index].image) && capture.rendered;
        }
    }
}

bool D3D12GraphicsDevice::SubmitCapture(
    const Rendering::RenderFrame& frame, const unsigned int channel,
    bool& advanceHeadlessFrame, SubmittedCapture& capture)
{
    capture = {};

    if (mSubmissionFailed)
    {
        Diagnostics::Debug::LogError("D3D12 cannot capture after a command submission failure.");
        return false;
    }
    if (mFrameInProgress)
    {
        Diagnostics::Debug::LogError(
            "A capture cannot be taken between BeginFrame and EndFrame: it resets the command list, "
            "which would discard the frame recorded so far.");
        return false;
    }
    if (channel >= MaxDeferredCaptureChannels)
    {
        Diagnostics::Debug::LogError(
            "D3D12 has no frame slots for this capture channel. channel=", channel,
            ", channels=", MaxDeferredCaptureChannels);
        return false;
    }

    const Rendering::RenderTargetSize size = frame.GetRenderTargetSize();
    if (size.width == 0 || size.height == 0)
    {
        Diagnostics::Debug::LogError("A frame with no size cannot be rendered to an image.");
        return false;
    }
    CaptureTarget* const target = EnsureCaptureTarget(channel, size.width, size.height);
    if (!target)
    {
        return false;
    }

    // This side was last used two captures ago; its work must be done before its allocator and
    // readback buffer are reused. Waiting for the other side — the one a deferred request reads —
    // comes after the new capture is submitted, so the GPU has that much longer to finish it.
    const unsigned int parity = target->writeParity;
    CaptureTarget::Parity& side = target->parities[parity];
    // A headless batch advances residency once, after earlier submissions finish. Subsequent
    // captures retain that cache frame so they cannot evict resources the batch is still using.
    if (advanceHeadlessFrame && !WaitForFenceValue(mFenceValue))
    {
        return false;
    }
    if (!WaitForFenceValue(side.fenceValue))
    {
        return false;
    }

    const HRESULT allocatorResult = side.commandAllocator->Reset();
    if (FAILED(allocatorResult))
    {
        LogHResult("Resetting the D3D12 capture allocator", allocatorResult);
        return false;
    }
    const HRESULT listResult = mCommandList->Reset(side.commandAllocator.Get(), nullptr);
    if (FAILED(listResult))
    {
        LogHResult("Resetting the D3D12 command list for a capture", listResult);
        return false;
    }

    // Captures own frame slots of their own past the main frames', so a capture executing while a
    // main frame is recorded never shares its constant segment or upload buffers.
    const UINT frameSlot = FramesInFlight + channel * 2 + parity;
    if (advanceHeadlessFrame)
    {
        mRenderer->BeginFrame(frameSlot);
        advanceHeadlessFrame = false;
    }
    else
    {
        mRenderer->BeginCapture(frameSlot);
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE renderTargetHandle =
        target->renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_CPU_DESCRIPTOR_HANDLE depthStencilHandle =
        target->depthStencilViewHeap->GetCPUDescriptorHandleForHeapStart();
    mCommandList->OMSetRenderTargets(1, &renderTargetHandle, FALSE, &depthStencilHandle);

    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(size.width);
    viewport.Height = static_cast<float>(size.height);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissorRectangle = {};
    scissorRectangle.right = static_cast<LONG>(size.width);
    scissorRectangle.bottom = static_cast<LONG>(size.height);
    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissorRectangle);

    const Math::Color clearColor = Rendering::GetLinearClearColor(frame);
    const float clearValues[] = { clearColor.r, clearColor.g, clearColor.b, clearColor.a };
    mCommandList->ClearRenderTargetView(renderTargetHandle, clearValues, 0, nullptr);
    mCommandList->ClearDepthStencilView(
        depthStencilHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // A rejected frame still produces an image: the cleared target is the honest answer, and a
    // caller comparing two backends wants to see it rather than an error with no pixels.
    const bool rendered = mRenderer->Render(frame);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target->colorTexture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    mCommandList->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = target->colorTexture.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = side.readbackBuffer.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = target->footprint;
    mCommandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

    // Back to where the next capture expects to find it, so repeating one needs no rebuild.
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    mCommandList->ResourceBarrier(1, &barrier);

    const HRESULT closeResult = mCommandList->Close();
    if (FAILED(closeResult))
    {
        mSubmissionFailed = true;
        LogHResult("Closing the D3D12 capture command list", closeResult);
        static_cast<void>(ReportDeviceRemovalIfLost(closeResult));
        return false;
    }
    ID3D12CommandList* commandLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, commandLists);

    const UINT64 fenceValue = ++mFenceValue;
    const HRESULT signalResult = mCommandQueue->Signal(mFence.Get(), fenceValue);
    if (FAILED(signalResult))
    {
        mSubmissionFailed = true;
        LogHResult("Signalling the D3D12 capture fence", signalResult);
        static_cast<void>(ReportDeviceRemovalIfLost(signalResult));
        return false;
    }
    side.fenceValue = fenceValue;
    target->writeParity ^= 1u;
    DrainDebugMessages();
    capture = { target, parity, rendered };
    return true;
}

bool D3D12GraphicsDevice::WaitForFrameSlot(const UINT frameIndex)
{
    if (!mFence || !mFenceEvent)
    {
        return true;
    }
    const UINT64 awaited = mFrameFenceValues[frameIndex];
    if (awaited == 0 || mFence->GetCompletedValue() >= awaited)
    {
        return true;
    }
    const HRESULT result = mFence->SetEventOnCompletion(awaited, mFenceEvent);
    if (FAILED(result))
    {
        LogHResult("Waiting for a D3D12 frame slot", result);
        static_cast<void>(ReportDeviceRemovalIfLost(result));
        return false;
    }
    return WaitForFenceEvent("Waiting for a D3D12 frame slot");
}

bool D3D12GraphicsDevice::CreateDevice()
{
#if defined(DEBUG) || defined(_DEBUG)
    // D3D11 enables its debug layer in debug builds; do the same here so D3D12 resource-usage and
    // binding errors are reported instead of surfacing as silent corruption or device removal.
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.ReleaseAndGetAddressOf()))))
    {
        debugController->EnableDebugLayer();
    }
    else
    {
        Diagnostics::Debug::LogWarning(
            "The D3D12 debug layer is unavailable. Install the Graphics Tools optional feature to "
            "enable Direct3D 12 validation.");
    }
#endif

    HRESULT result = CreateDXGIFactory1(IID_PPV_ARGS(mFactory.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("CreateDXGIFactory1", result);
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0;
         mFactory->EnumAdapters1(index, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND;
         ++index)
    {
        DXGI_ADAPTER_DESC1 descriptor = {};
        adapter->GetDesc1(&descriptor);
        if ((descriptor.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(
                adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(mDevice.ReleaseAndGetAddressOf()))))
        {
#if defined(DEBUG) || defined(_DEBUG)
            // Queried only where the debug layer is enabled, so the messages this drains exist.
            if (SUCCEEDED(mDevice.As(&mInfoQueue)))
            {
                // A capture target is created without an optimized clear value, because the colour
                // to clear it to belongs to the frame being captured and is not known when the
                // target is made. The layer warns that such a clear is slower than an optimized one,
                // which is true and is the intended trade: the alternative is rebuilding the target
                // whenever a camera's background colour changes. The warning is dropped here rather
                // than forwarded, because a warning that appears on every capture teaches the reader
                // of the log to ignore warnings.
                D3D12_MESSAGE_ID suppressedMessages[] = {
                    D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE
                };
                D3D12_INFO_QUEUE_FILTER filter = {};
                filter.DenyList.NumIDs = static_cast<UINT>(std::size(suppressedMessages));
                filter.DenyList.pIDList = suppressedMessages;
                if (FAILED(mInfoQueue->AddStorageFilterEntries(&filter)))
                {
                    Diagnostics::Debug::LogWarning(
                        "The D3D12 info queue rejected its message filter; capture clears will warn.");
                }

                // Said out loud so an empty log reads as "the debug layer found nothing" rather
                // than "the debug layer was never asked".
                Diagnostics::Debug::Log("D3D12 validation messages are forwarded to this log.");
            }
            else
            {
                Diagnostics::Debug::LogWarning(
                    "The D3D12 info queue is unavailable; validation messages stay in the debugger.");
            }
#endif
            return true;
        }
    }

    // Only hardware adapters are accepted, matching IsHardwareSupported. Falling back to the default
    // adapter here could silently select the software rasterizer that support detection rejected.
    Diagnostics::Debug::LogError(
        "No Direct3D 12 hardware adapter could create a device at feature level 11_0.");
    return false;
}

bool D3D12GraphicsDevice::CreateCommandObjects()
{
    D3D12_COMMAND_QUEUE_DESC queueDescriptor = {};
    queueDescriptor.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HRESULT result = mDevice->CreateCommandQueue(
        &queueDescriptor, IID_PPV_ARGS(mCommandQueue.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D12 command queue", result);
        return false;
    }

    for (Microsoft::WRL::ComPtr<ID3D12CommandAllocator>& allocator : mCommandAllocators)
    {
        result = mDevice->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            LogHResult("Failed to create the D3D12 command allocator", result);
            return false;
        }
    }
    result = mDevice->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, mCommandAllocators[0].Get(), nullptr,
        IID_PPV_ARGS(mCommandList.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D12 command list", result);
        return false;
    }
    result = mCommandList->Close();
    if (FAILED(result))
    {
        LogHResult("Failed to close the initial D3D12 command list", result);
        return false;
    }

    result = mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(mFence.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D12 fence", result);
        return false;
    }
    mFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!mFenceEvent)
    {
        Diagnostics::Debug::LogError("Failed to create the D3D12 fence event.");
        return false;
    }
    return true;
}

bool D3D12GraphicsDevice::CreateSwapChain(const UINT width, const UINT height)
{
    DXGI_SWAP_CHAIN_DESC1 descriptor = {};
    descriptor.Width = width;
    descriptor.Height = height;
    descriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    descriptor.SampleDesc.Count = 1;
    descriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    descriptor.BufferCount = FrameCount;
    descriptor.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
    HRESULT result = mFactory->CreateSwapChainForHwnd(
        mCommandQueue.Get(), mWindowHandle, &descriptor, nullptr, nullptr,
        swapChain.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D12 swap chain", result);
        return false;
    }
    if (!Direct3D::DisableDxgiWindowMonitoring(swapChain.Get(), mWindowHandle))
    {
        return false;
    }
    result = swapChain.As(&mSwapChain);
    if (FAILED(result))
    {
        LogHResult("Failed to query IDXGISwapChain3", result);
        return false;
    }
    mCurrentFrameIndex = mSwapChain->GetCurrentBackBufferIndex();
    return true;
}

bool D3D12GraphicsDevice::CreateRenderTargetViews()
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDescriptor = {};
    heapDescriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDescriptor.NumDescriptors = FrameCount;
    HRESULT result = mDevice->CreateDescriptorHeap(
        &heapDescriptor, IID_PPV_ARGS(mRenderTargetViewHeap.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D12 RTV heap", result);
        return false;
    }

    mRenderTargetViewDescriptorSize =
        mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        mRenderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < FrameCount; ++index)
    {
        result = mSwapChain->GetBuffer(
            index, IID_PPV_ARGS(mRenderTargets[index].ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            LogHResult("Failed to get a D3D12 swap-chain buffer", result);
            return false;
        }
        // 버퍼는 UNORM, 뷰는 sRGB. 파이프라인이 이름 짓는 RTV 포맷과 같아야 한다.
        D3D12_RENDER_TARGET_VIEW_DESC viewDescriptor = {};
        viewDescriptor.Format = RenderTargetFormat;
        viewDescriptor.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        mDevice->CreateRenderTargetView(mRenderTargets[index].Get(), &viewDescriptor, handle);
        handle.ptr += mRenderTargetViewDescriptorSize;
    }
    return true;
}

bool D3D12GraphicsDevice::CreateDepthStencilView(const UINT width, const UINT height)
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDescriptor = {};
    heapDescriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDescriptor.NumDescriptors = 1;
    HRESULT result = mDevice->CreateDescriptorHeap(
        &heapDescriptor, IID_PPV_ARGS(mDepthStencilViewHeap.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D12 DSV heap", result);
        return false;
    }

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC textureDescriptor = {};
    textureDescriptor.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDescriptor.Width = width;
    textureDescriptor.Height = height;
    textureDescriptor.DepthOrArraySize = 1;
    textureDescriptor.MipLevels = 1;
    textureDescriptor.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    textureDescriptor.SampleDesc.Count = 1;
    textureDescriptor.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDescriptor.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = textureDescriptor.Format;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;
    result = mDevice->CreateCommittedResource(
        &heapProperties, D3D12_HEAP_FLAG_NONE, &textureDescriptor,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
        IID_PPV_ARGS(mDepthStencilTexture.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Failed to create the D3D12 depth-stencil texture", result);
        return false;
    }
    mDevice->CreateDepthStencilView(
        mDepthStencilTexture.Get(), nullptr,
        mDepthStencilViewHeap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

bool D3D12GraphicsDevice::ResizeIfNeeded()
{
    RECT clientRectangle = {};
    if (!GetClientRect(mWindowHandle, &clientRectangle))
    {
        Diagnostics::Debug::LogError("Failed to query the D3D12 window client size.");
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
    if (!WaitForGpu())
    {
        return false;
    }
    for (Microsoft::WRL::ComPtr<ID3D12Resource>& renderTarget : mRenderTargets)
    {
        renderTarget.Reset();
    }
    mDepthStencilTexture.Reset();
    mRenderTargetViewHeap.Reset();
    mDepthStencilViewHeap.Reset();
    const HRESULT result = mSwapChain->ResizeBuffers(
        FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    if (FAILED(result))
    {
        LogHResult("Failed to resize D3D12 swap-chain buffers", result);
        return false;
    }
    mCurrentFrameIndex = mSwapChain->GetCurrentBackBufferIndex();
    if (!CreateRenderTargetViews() || !CreateDepthStencilView(width, height))
    {
        return false;
    }
    SetViewport(width, height);
    return true;
}

bool D3D12GraphicsDevice::WaitForGpu()
{
    if (!mCommandQueue || !mFence || !mFenceEvent)
    {
        return true;
    }
    const UINT64 fenceValue = ++mFenceValue;
    HRESULT result = mCommandQueue->Signal(mFence.Get(), fenceValue);
    if (FAILED(result))
    {
        LogHResult("Failed to signal the D3D12 fence", result);
        static_cast<void>(ReportDeviceRemovalIfLost(result));
        return false;
    }
    if (mFence->GetCompletedValue() < fenceValue)
    {
        result = mFence->SetEventOnCompletion(fenceValue, mFenceEvent);
        if (FAILED(result))
        {
            LogHResult("Failed to wait for the D3D12 fence", result);
            static_cast<void>(ReportDeviceRemovalIfLost(result));
            return false;
        }
        return WaitForFenceEvent("Waiting for the D3D12 fence");
    }
    return true;
}

void D3D12GraphicsDevice::SetViewport(const UINT width, const UINT height)
{
    mViewport.TopLeftX = 0.0f;
    mViewport.TopLeftY = 0.0f;
    mViewport.Width = static_cast<float>(width);
    mViewport.Height = static_cast<float>(height);
    mViewport.MinDepth = 0.0f;
    mViewport.MaxDepth = 1.0f;
    mScissorRectangle.left = 0;
    mScissorRectangle.top = 0;
    mScissorRectangle.right = static_cast<LONG>(width);
    mScissorRectangle.bottom = static_cast<LONG>(height);
}

}
