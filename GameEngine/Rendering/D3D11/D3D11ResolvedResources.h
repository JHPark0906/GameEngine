#pragma once

#include <cstddef>
#include <cstdint>

#include <d3d11.h>
#include <wrl/client.h>

#include "../ShaderInterop.h"
#include "../RenderCommandList.h"
#include "../RenderFrame.h"

namespace GameEngine::Rendering::D3D11
{

/// <summary>resolve된 GeometryHandle에 대해 캐시된 D3D11 정점/인덱스 버퍼이다.</summary>
class D3D11ResolvedGeometry final : public IResolvedGeometry
{
public:
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
    UINT indexCount = 0;
    /// <summary>이 항목이 쥔 GPU 바이트이다. 공용 메시 캐시 예산에 청구된다.</summary>
    std::size_t byteSize = 0;

    [[nodiscard]] bool IsDrawable() const override
    {
        return vertexBuffer && indexBuffer && indexCount > 0;
    }

    [[nodiscard]] const char* GetBackendName() const override { return "D3D11"; }
};

/// <summary>resolve된 SkinnedGeometryHandle에 대해 캐시된 D3D11 정점/인덱스 버퍼이다. 정점
/// 레이아웃이 <see cref="D3D11ResolvedGeometry"/>와 다를 뿐 그 밖의 계약은 같다.</summary>
class D3D11ResolvedSkinnedGeometry final : public IResolvedGeometry
{
public:
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
    UINT indexCount = 0;
    std::size_t byteSize = 0;

    [[nodiscard]] bool IsDrawable() const override
    {
        return vertexBuffer && indexBuffer && indexCount > 0;
    }

    [[nodiscard]] const char* GetBackendName() const override { return "D3D11"; }
};

/// <summary>머티리얼 또는 래스터화된 텍스트 요청에 대해 캐시된 D3D11 셰이더 리소스 뷰이다.</summary>
class D3D11ResolvedTexture final : public IResolvedTexture
{
public:
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResourceView;
    /// <summary>뷰가 보는 텍스처다. 제자리 갱신이 여기에 픽셀을 다시 쓴다.</summary>
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    /// <summary>마지막으로 올린 픽셀의 revision이다. 소스와 다르면 다시 올린다.</summary>
    std::uint64_t revision = 0;
    /// <summary>제자리 갱신이 가능한지다. 바뀌지 않는 에셋은 불변으로 만들어 더 빠르다.</summary>
    bool updatable = false;
    UINT width = 0;
    UINT height = 0;
    /// <summary>이 항목이 쥔 GPU 바이트이다. 공용 텍스처 또는 텍스트 예산에 청구된다.</summary>
    std::size_t byteSize = 0;

    [[nodiscard]] bool IsDrawable() const override
    {
        return shaderResourceView && width > 0 && height > 0;
    }

    [[nodiscard]] const char* GetBackendName() const override { return "D3D11"; }

    [[nodiscard]] unsigned int GetPixelWidth() const override { return width; }
    [[nodiscard]] unsigned int GetPixelHeight() const override { return height; }
};

}
