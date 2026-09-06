#pragma once

#include <memory>

#include "../RenderFrame.h"

namespace GameEngine::Runtime
{
class Game;
}

namespace GameEngine::Rendering::D3D11
{

class ID3D11GraphicsDevice;
class D3D11ResourceResolver;
class D3D11CommandList;

/// <summary>
/// 렌더링의 D3D11 쪽을 조립한다: 이 백엔드의 캐시를 소유하는 리소스 리졸버와, 공용 렌더 패스가
/// 기록에 사용하는 command list이다.
/// </summary>
class D3D11Renderer final
{
public:
    D3D11Renderer();
    ~D3D11Renderer();

    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    [[nodiscard]] bool Initialize(ID3D11GraphicsDevice& graphicsDevice);
    void BeginFrame();
    /// <summary>캡처의 시작이다. 명령 상태는 새로 시작하되 캐시의 프레임은 올리지 않는다.</summary>
    void BeginCapture();
    [[nodiscard]] bool Render(const Rendering::RenderFrame& frame);

private:
    std::unique_ptr<D3D11ResourceResolver> mResourceResolver;
    std::unique_ptr<D3D11CommandList> mCommandList;
};

}
