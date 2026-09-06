#pragma once


#include <array>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <memory>
#include <span>
#include <unordered_map>
#include <wrl/client.h>

#include "D3D12FrameResources.h"
#include "ID3D12GraphicsDevice.h"

namespace GameEngine::Rendering::D3D12
{

class D3D12Renderer;

/// <summary>Direct3D 12 장치, swap chain, 명령 제출, 기본 타깃들이다.</summary>
class D3D12GraphicsDevice final : public ID3D12GraphicsDevice
{
public:
    D3D12GraphicsDevice();
    ~D3D12GraphicsDevice() override;

    D3D12GraphicsDevice(const D3D12GraphicsDevice&) = delete;
    D3D12GraphicsDevice& operator=(const D3D12GraphicsDevice&) = delete;
    D3D12GraphicsDevice(D3D12GraphicsDevice&&) = delete;
    D3D12GraphicsDevice& operator=(D3D12GraphicsDevice&&) = delete;

    [[nodiscard]] static bool IsHardwareSupported();
    [[nodiscard]] bool Initialize(const Platform::NativeSurface& surface) override;
    [[nodiscard]] bool BeginFrame() override;
    [[nodiscard]] Rendering::RenderTargetSize GetRenderTargetSize() const override;
    [[nodiscard]] Rendering::GraphicsDeviceCapabilities GetCapabilities() const override;
    [[nodiscard]] bool Render(const Rendering::RenderFrame& frame) override;
    [[nodiscard]] bool EndFrame() override;

    /// <summary>
    /// 프레임을 오프스크린 타깃에 렌더링하고 픽셀을 회수한다. 채널마다 명령 할당자와 리드백
    /// 버퍼를 둘씩 번갈아 쓰고, 지연 요청이면 이번 캡처의 펜스 대신 지난 캡처의 펜스만
    /// 확인하고 그 리드백을 매핑한다. 캡처는 주 프레임과 다른 프레임 슬롯을 쓰므로 GPU에서
    /// 서로 겹쳐 실행되어도 상수나 업로드 버퍼를 나눠 갖지 않는다.
    /// </summary>
    [[nodiscard]] bool RenderToImage(
        const Rendering::RenderFrame& frame, Rendering::CapturedImage& image,
        const Rendering::IGraphicsDevice::CaptureRequest& request = {}) override;
    void RenderToImages(std::span<CaptureBatchItem> items) override;

    [[nodiscard]] ID3D12Device* GetDevice() const override { return mDevice.Get(); }
    [[nodiscard]] ID3D12GraphicsCommandList* GetCommandList() const override { return mCommandList.Get(); }

private:
    /// <summary>슬롯마다 command allocator, 상수 링 세그먼트, swap-chain 버퍼가 하나씩이다.</summary>
    static constexpr UINT FrameCount = FramesInFlight;

    [[nodiscard]] bool CreateDevice();
    [[nodiscard]] bool CreateCommandObjects();
    [[nodiscard]] bool CreateSwapChain(UINT width, UINT height);
    [[nodiscard]] bool CreateRenderTargetViews();
    [[nodiscard]] bool CreateDepthStencilView(UINT width, UINT height);
    [[nodiscard]] bool ResizeIfNeeded();
    /// <summary>아직 in flight인 모든 프레임을 기다린다. 해체하거나 크기를 바꿀 때 쓴다.</summary>
    [[nodiscard]] bool WaitForGpu();

    /// <summary>
    /// 캡처가 그려 넣고 읽어 오는 데 쓰는 것들이다.
    ///
    /// 프레임 힙의 슬롯 하나가 아니라 자기만의 descriptor 힙을 갖는다: 프레임 힙은 프레임
    /// 슬롯으로 인덱싱되는데, 그 끝에 의미가 다른 항목을 덧붙이는 것은 인덱싱 실수를 컴파일
    /// 오류가 아니라 잘못된 렌더 타깃으로 만드는 길이다.
    /// </summary>
    /// <summary>캡처 채널 하나의 자원이다. 렌더 타깃은 하나, 큐가 순서를 지키므로 충분하다.</summary>
    struct CaptureTarget
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> colorTexture;
        Microsoft::WRL::ComPtr<ID3D12Resource> depthTexture;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> renderTargetViewHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> depthStencilViewHeap;
        /// <summary>
        /// 둘을 번갈아 쓴다: 한 캡처가 GPU에서 실행되는 동안 다음 캡처가 다른 쪽을 기록하고,
        /// CPU는 펜스가 지난 쪽의 리드백을 읽는다.
        /// </summary>
        struct Parity
        {
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
            Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer;
            /// <summary>이 쪽의 마지막 캡처가 도달할 펜스 값이다. 0이면 아직 캡처가 없다.</summary>
            UINT64 fenceValue = 0;
        };
        Parity parities[2];
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT rows = 0;
        /// <summary>
        /// 리드백 버퍼의 실제 크기다. 행 pitch × 행 수보다 작다: 마지막 행은 패딩되지 않는다.
        /// Map의 읽기 범위가 이것을 넘으면 디버그 레이어가 거절한다.
        /// </summary>
        UINT64 readbackSize = 0;
        UINT width = 0;
        UINT height = 0;
        /// <summary>다음 캡처가 기록할 쪽이다.</summary>
        unsigned int writeParity = 0;
    };

    /// <summary>같은 동기 호출 안에서 회수할, 이미 제출한 현재 프레임이다.</summary>
    struct SubmittedCapture
    {
        CaptureTarget* target = nullptr;
        unsigned int parity = 0;
        bool rendered = false;
    };

    /// <summary>
    /// 그리기와 복사를 제출한다. headless 캐시를 전진시키기 전에는 앞선 GPU 작업을 기다리고,
    /// 같은 배치의 후속 캡처는 캐시의 나이를 늘리지 않는다.
    /// </summary>
    [[nodiscard]] bool SubmitCapture(
        const Rendering::RenderFrame& frame, unsigned int channel,
        bool& advanceHeadlessFrame, SubmittedCapture& capture);

    /// <summary>채널의 캡처 타깃을 크기에 맞게 준비한다. 다시 만들 때는 GPU를 먼저 기다린다.</summary>
    [[nodiscard]] CaptureTarget* EnsureCaptureTarget(unsigned int channel, UINT width, UINT height);
    [[nodiscard]] bool WaitForFenceValue(UINT64 fenceValue);
    [[nodiscard]] bool ReadCapture(
        const CaptureTarget& target, unsigned int parity, Rendering::CapturedImage& image) const;

    std::unordered_map<unsigned int, CaptureTarget> mCaptureTargets;

    /// <summary>
    /// BeginFrame과 EndFrame 사이에 설정된다. 캡처는 command list를 리셋하므로, 프레임이 열려
    /// 있는 동안 캡처하면 그때까지 기록된 모든 것을 버리게 된다.
    /// </summary>
    bool mFrameInProgress = false;

    /// <summary>
    /// 제출 실패 뒤에는 캐시의 업로드 상태나 슬롯 펜스로 GPU 사용 완료를 증명할 수 없다.
    /// 장치를 다시 만들기 전까지 프레임과 캡처를 막아 자원을 재사용하지 않는다.
    /// </summary>
    bool mSubmissionFailed = false;

    /// <summary>
    /// 이 슬롯이 지난번 사용될 때 기록된 작업이 끝날 때까지만 기다린다. 그래야 슬롯의
    /// allocator, 상수 세그먼트, 스테이징 버퍼를 재사용할 수 있게 된다.
    /// </summary>
    [[nodiscard]] bool WaitForFrameSlot(UINT frameIndex);

    /// <summary>
    /// 펜스 이벤트가 신호될 때까지 기다린다. 기한이 있는 이유는 장치 손실 때문이다: GPU가
    /// 사라지면 그 이벤트를 신호할 것이 남아 있지 않아, 무한 대기는 종료조차 막는 행으로
    /// 끝난다. 기한을 넘기면 왜 멈췄는지 — 제거 사유까지 — 말하고 실패로 돌아온다.
    /// </summary>
    /// <param name="operation">기다리던 일이다. 메시지가 문장으로 읽히도록 표현한다.</param>
    /// <returns>신호를 받았으면 true이다.</returns>
    [[nodiscard]] bool WaitForFenceEvent(const char* operation);

    /// <summary>
    /// 장치가 사라졌는지 묻고, 맞으면 사유를 로그로 남긴다. 실패 경로들이 공유한다.
    /// </summary>
    /// <param name="operationResult">실패한 연산이 반환한 코드이다.</param>
    /// <returns>장치 손실이었으면 true이다.</returns>
    bool ReportDeviceRemovalIfLost(HRESULT operationResult) const;

    void DrainDebugMessages();
    void SetViewport(UINT width, UINT height);

    Microsoft::WRL::ComPtr<IDXGIFactory4> mFactory;
    Microsoft::WRL::ComPtr<ID3D12Device> mDevice;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> mInfoQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, FrameCount> mCommandAllocators;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> mSwapChain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRenderTargetViewHeap;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> mRenderTargets;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDepthStencilViewHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilTexture;
    std::unique_ptr<D3D12Renderer> mRenderer;
    Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
    HANDLE mFenceEvent = nullptr;
    UINT64 mFenceValue = 0;
    /// <summary>각 슬롯에서 제출된 작업이 완료될 때 도달할 펜스 값이다.</summary>
    std::array<UINT64, FrameCount> mFrameFenceValues{};
    UINT mRenderTargetViewDescriptorSize = 0;
    UINT mCurrentFrameIndex = 0;
    D3D12_VIEWPORT mViewport = {};
    D3D12_RECT mScissorRectangle = {};
    HWND mWindowHandle = nullptr;
};

}
