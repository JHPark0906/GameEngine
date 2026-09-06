#pragma once


#include <d3d11.h>
#include <memory>
#include <unordered_map>
#include <wrl/client.h>

#include "ID3D11GraphicsDevice.h"

namespace GameEngine::Rendering::D3D11
{

class D3D11Renderer;

/// <summary>Direct3D 11 장치, 스왑 체인 및 기본 렌더 타깃을 관리한다.</summary>
class D3D11GraphicsDevice final : public ID3D11GraphicsDevice
{
public:
    D3D11GraphicsDevice();
    ~D3D11GraphicsDevice() override;

    D3D11GraphicsDevice(const D3D11GraphicsDevice&) = delete;
    D3D11GraphicsDevice& operator=(const D3D11GraphicsDevice&) = delete;
    D3D11GraphicsDevice(D3D11GraphicsDevice&&) = delete;
    D3D11GraphicsDevice& operator=(D3D11GraphicsDevice&&) = delete;

    /// <summary>Win32 창에 연결된 Direct3D 11 출력 자원을 생성한다.</summary>
    /// <param name="surface">출력 대상으로 사용할 Win32 표면이다.</param>
    /// <returns>장치와 프레임 출력 자원을 모두 생성했으면 true이다.</returns>
    [[nodiscard]] bool Initialize(const Platform::NativeSurface& surface) override;

    /// <summary>기본 렌더 타깃과 뷰포트를 바인딩한다. 화면을 지우는 것은 Render의 책임이다.</summary>
    [[nodiscard]] bool BeginFrame() override;
    [[nodiscard]] Rendering::RenderTargetSize GetRenderTargetSize() const override;
    [[nodiscard]] Rendering::GraphicsDeviceCapabilities GetCapabilities() const override;
    [[nodiscard]] bool Render(const Rendering::RenderFrame& frame) override;

    /// <summary>수직 동기화를 사용해 스왑 체인의 백 버퍼를 표시한다.</summary>
    /// <returns>Present 호출에 성공했으면 true이다.</returns>
    [[nodiscard]] bool EndFrame() override;

    /// <summary>
    /// 프레임을 오프스크린 타깃에 렌더링하고 픽셀을 회수한다. 지연 요청이면 스테이징 텍스처
    /// 둘을 번갈아 써서, 이번 프레임의 복사를 기록해 두고 지난 프레임의 것을 매핑한다 — 매핑이
    /// 기다리는 복사는 한 프레임 전의 것이라 대개 끝나 있다.
    /// </summary>
    [[nodiscard]] bool RenderToImage(
        const Rendering::RenderFrame& frame, Rendering::CapturedImage& image,
        const Rendering::IGraphicsDevice::CaptureRequest& request = {}) override;

    /// <summary>렌더링 자원 생성에 사용할 Direct3D 11 장치를 반환한다.</summary>
    /// <returns>소유하지 않는 ID3D11Device 포인터이다.</returns>
    [[nodiscard]] ID3D11Device* GetDevice() const override { return mDevice.Get(); }

    /// <summary>렌더링 명령을 기록할 즉시 컨텍스트를 반환한다.</summary>
    /// <returns>소유하지 않는 ID3D11DeviceContext 포인터이다.</returns>
    [[nodiscard]] ID3D11DeviceContext* GetDeviceContext() const override
    {
        return mDeviceContext.Get();
    }

private:
    /// <summary>
    /// 캡처가 그려 넣는 오프스크린 타깃과 CPU가 읽을 수 있는 스테이징 사본이다.
    ///
    /// 캡처 사이에 유지되고 크기가 바뀔 때만 다시 만든다. 에디터 패널은 매 프레임 같은 크기로
    /// 캡처하는데, 그때마다 텍스처 세 장을 다시 만드는 것이 패널 하나를 보여주는 비용의
    /// 대부분이 됐을 것이기 때문이다.
    /// </summary>
    struct CaptureTarget
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> colorTexture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> colorView;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthView;
        /// <summary>둘을 번갈아 쓴다: 지연 캡처가 하나에 복사하는 동안 다른 하나를 매핑한다.</summary>
        Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTextures[2];
        UINT width = 0;
        UINT height = 0;
        /// <summary>다음 복사가 갈 스테이징 텍스처다.</summary>
        unsigned int writeIndex = 0;
        /// <summary>지연 캡처가 아직 매핑하지 않은 복사가 기다리고 있는지다.</summary>
        bool hasPendingCopy = false;
    };

    /// <summary>채널의 캡처 타깃을 크기에 맞게 준비한다. 크기가 바뀌면 다시 만든다.</summary>
    [[nodiscard]] CaptureTarget* EnsureCaptureTarget(unsigned int channel, UINT width, UINT height);

    /// <summary>headless 초기화가 쓰는, swap chain 없는 장치 생성이다.</summary>
    [[nodiscard]] bool CreateDeviceWithoutSwapChain();

    /// <summary>
    /// BeginFrame과 EndFrame 사이에 설정된다. 캡처는 렌더 타깃을 다시 바인딩하므로, 프레임이
    /// 열려 있는 동안 캡처하면 그 프레임이 present되지 않는 곳에 그리게 된다.
    /// </summary>
    bool mFrameInProgress = false;

    /// <summary>채널별 캡처 타깃이다. 에디터의 게임 뷰와 씬 뷰가 각자 하나씩 갖는다.</summary>
    std::unordered_map<unsigned int, CaptureTarget> mCaptureTargets;

    /// <summary>flip 모델 swap chain의 백 버퍼 수이다. flip 모델은 최소 두 개를 요구한다.</summary>
    static constexpr UINT BufferCount = 2;

    [[nodiscard]] bool CreateDeviceAndSwapChain(HWND windowHandle);
    [[nodiscard]] bool CreateRenderTargetView();
    [[nodiscard]] bool CreateDepthStencilView(UINT width, UINT height);
    [[nodiscard]] bool ResizeIfNeeded();
    void SetViewport(UINT width, UINT height);

    /// <summary>
    /// Direct3D 디버그 레이어 메시지를 엔진 로그로 전달한다. 이것이 없으면 디버그 레이어는
    /// 붙어 있는 디버거에만 닿아서, 플레이어를 터미널에서 실행하면 바인딩·리소스 오류가 보이지
    /// 않는다.
    /// </summary>
    void DrainDebugMessages();

    Microsoft::WRL::ComPtr<ID3D11Device> mDevice;
    Microsoft::WRL::ComPtr<ID3D11InfoQueue> mInfoQueue;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> mDeviceContext;
    Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> mRenderTargetView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> mDepthStencilTexture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> mDepthStencilView;
    D3D11_VIEWPORT mViewport = {};
    HWND mWindowHandle = nullptr;
    std::unique_ptr<D3D11Renderer> mRenderer;
};

}
