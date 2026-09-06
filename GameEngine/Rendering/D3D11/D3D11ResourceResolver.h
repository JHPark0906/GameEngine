#pragma once

#include <memory>

#include "D3D11ResolvedResources.h"
#include "../RenderBackendInterfaces.h"

namespace GameEngine::Rendering::D3D11
{

class ID3D11GraphicsDevice;

/// <summary>Owns D3D11 mesh, image-texture, and rasterized-text caches.</summary>
class D3D11ResourceResolver final
{
public:
    D3D11ResourceResolver();
    ~D3D11ResourceResolver();

    D3D11ResourceResolver(const D3D11ResourceResolver&) = delete;
    D3D11ResourceResolver& operator=(const D3D11ResourceResolver&) = delete;

    [[nodiscard]] bool Initialize(ID3D11GraphicsDevice& graphicsDevice);

    /// <param name="advanceCaches">
    /// 캐시의 프레임을 올릴지다. 주 프레임은 올리고 캡처는 올리지 않는다 — 캡처마다 올리면
    /// 캐시가 세는 "프레임"이 실제 프레임보다 빨리 흘러 유휴 만료가 앞당겨진다.
    /// </param>
    void BeginFrame(bool advanceCaches = true);
    [[nodiscard]] const D3D11ResolvedGeometry* ResolveGeometry(
        const RenderFrame& frame, GeometryHandle handle);
    [[nodiscard]] const D3D11ResolvedSkinnedGeometry* ResolveSkinnedGeometry(
        const RenderFrame& frame, SkinnedGeometryHandle handle);
    [[nodiscard]] const D3D11ResolvedTexture* ResolveMaterial(
        const RenderFrame& frame, MaterialHandle handle);
    [[nodiscard]] const D3D11ResolvedTexture* ResolveText(const TextDraw& draw);

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

static_assert(
    RenderResourceResolver<D3D11ResourceResolver>,
    "A backend resource resolver must satisfy the shared resolve contract.");

}
