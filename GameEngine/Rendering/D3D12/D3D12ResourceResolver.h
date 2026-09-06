#pragma once

#include <memory>

#include "D3D12ResolvedResources.h"
#include "../RenderBackendInterfaces.h"

namespace GameEngine::Rendering::D3D12
{

class ID3D12GraphicsDevice;

/// <summary>
/// D3D12의 메시·이미지 텍스처·래스터화된 텍스트 캐시를 소유한다. 프레임 핸들을 CPU·버퍼
/// 리소스로만 resolve한다. command list, descriptor 힙, descriptor 인덱스는
/// D3D12TextureUploader의 것이라, 리소스 resolve는 명령 기록과 독립적으로 남는다.
/// </summary>
class D3D12ResourceResolver final
{
public:
    D3D12ResourceResolver();
    ~D3D12ResourceResolver();

    D3D12ResourceResolver(const D3D12ResourceResolver&) = delete;
    D3D12ResourceResolver& operator=(const D3D12ResourceResolver&) = delete;

    [[nodiscard]] bool Initialize(ID3D12GraphicsDevice& graphicsDevice);

    /// <param name="advanceCaches">
    /// 캐시의 프레임을 올릴지다. 주 프레임은 올리고 캡처는 올리지 않는다 — 캡처마다 올리면
    /// 캐시가 세는 "프레임"이 실제 프레임보다 빨리 흘러 유휴 만료가 앞당겨진다.
    /// </param>
    void BeginFrame(bool advanceCaches = true);
    [[nodiscard]] const D3D12ResolvedGeometry* ResolveGeometry(
        const RenderFrame& frame, GeometryHandle handle);
    [[nodiscard]] const D3D12ResolvedSkinnedGeometry* ResolveSkinnedGeometry(
        const RenderFrame& frame, SkinnedGeometryHandle handle);
    [[nodiscard]] const D3D12ResolvedTexture* ResolveMaterial(
        const RenderFrame& frame, MaterialHandle handle);
    [[nodiscard]] const D3D12ResolvedTexture* ResolveText(const TextDraw& draw);

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

static_assert(
    RenderResourceResolver<D3D12ResourceResolver>,
    "A backend resource resolver must satisfy the shared resolve contract.");

}
