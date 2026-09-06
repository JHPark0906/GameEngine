#pragma once

#include <memory>

#include <d3d12.h>

#include "../RenderFrame.h"

namespace GameEngine::Rendering::D3D12
{

class ID3D12GraphicsDevice;
class D3D12FrameResources;
class D3D12ResourceResolver;
class D3D12MeshUploader;
class D3D12SkinnedMeshUploader;
class D3D12TextureUploader;
class D3D12CommandList;

/// <summary>
/// 렌더링의 D3D12 쪽을 조립한다: 프레임 전역의 루트 시그니처·descriptor 힙·상수 링, 이 백엔드의
/// 캐시를 소유하는 리소스 리졸버와 텍스처 업로더, 그리고 공용 렌더 패스가 기록에 사용하는
/// command list이다.
/// </summary>
class D3D12Renderer final
{
public:
    D3D12Renderer();
    ~D3D12Renderer();

    D3D12Renderer(const D3D12Renderer&) = delete;
    D3D12Renderer& operator=(const D3D12Renderer&) = delete;

    [[nodiscard]] bool Initialize(ID3D12GraphicsDevice& graphicsDevice);
    void BeginFrame(UINT frameIndex);
    /// <summary>
    /// 캡처의 시작이다. 프레임 슬롯의 링과 업로드 버퍼는 주 프레임처럼 쓰되, 캐시의 프레임은
    /// 올리지 않는다.
    /// </summary>
    void BeginCapture(UINT frameIndex);
    [[nodiscard]] bool Render(const Rendering::RenderFrame& frame);

private:
    ID3D12GraphicsDevice* mGraphicsDevice = nullptr;
    /// <summary>가장 최근에 받은 프레임 번호다. Render가 command list의 BeginFrame에 그대로
    /// 건넨다 — 스킨드 메시의 뼈 상수 링이 주 프레임과 지연 캡처 채널을 가르는 데 필요하다.</summary>
    UINT mCurrentFrameIndex = 0;
    std::unique_ptr<D3D12FrameResources> mFrameResources;
    std::unique_ptr<D3D12ResourceResolver> mResourceResolver;
    std::unique_ptr<D3D12MeshUploader> mMeshUploader;
    std::unique_ptr<D3D12SkinnedMeshUploader> mSkinnedMeshUploader;
    std::unique_ptr<D3D12TextureUploader> mTextureUploader;
    std::unique_ptr<D3D12CommandList> mCommandList;
};

}
