#pragma once

#include <cstddef>
#include <memory>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "../../Assets/MeshData.h"
#include "../../Assets/SkinnedMeshData.h"
#include "../RenderCommandList.h"
#include "../RenderFrame.h"

namespace GameEngine::Rendering::D3D12
{

/// <summary>
/// GeometryHandle이 resolve되는 메시이다. GPU 버퍼가 아니라 프레임의 임포트된 정점을 쥔다.
/// default 힙 버퍼를 채우려면 command list가 필요한데 리졸버는 그것을 요구해선 안 되기
/// 때문이다. command list가 있는 자리에서 D3D12MeshUploader가 이것을 버퍼로 바꾼다. 텍스처
/// 경로가 쓰는 것과 같은 분할이다.
/// </summary>
class D3D12ResolvedGeometry final : public IResolvedGeometry
{
public:
    std::shared_ptr<const Assets::MeshData> mesh;

    [[nodiscard]] bool IsDrawable() const override { return mesh && mesh->IsValid(); }

    [[nodiscard]] const char* GetBackendName() const override { return "D3D12"; }
};

/// <summary>SkinnedGeometryHandle이 resolve되는 스킨드 메시다. <see cref="D3D12ResolvedGeometry"/>와
/// 같은 이유로 GPU 버퍼가 아니라 프레임의 임포트된 정점을 쥔다.</summary>
class D3D12ResolvedSkinnedGeometry final : public IResolvedGeometry
{
public:
    std::shared_ptr<const Assets::SkinnedMeshData> mesh;

    [[nodiscard]] bool IsDrawable() const override { return mesh && mesh->IsValid(); }

    [[nodiscard]] const char* GetBackendName() const override { return "D3D12"; }
};

/// <summary>
/// 머티리얼 또는 래스터화된 텍스트 요청을 위한, 디코딩된 CPU 픽셀이다. D3D12는 command list를
/// 통해 업로드하므로, resolve된 텍스처는 D3D12TextureUploader가 GPU로 복사할 원본 픽셀을 싣고
/// 다닌다. 리졸버 자신은 command list도 descriptor allocator도 결코 필요로 하지 않는다.
///
/// 픽셀은 첫 업로드 후 해제되지 않고 이 항목이 캐시에 있는 동안 유지된다. 리졸버의 텍스처
/// 캐시와 업로더의 바인딩 캐시는 키도 예산도 달라서, 이 항목이 아직 상주하는데 바인딩만 퇴거될
/// 수 있다. 픽셀을 해제하면 그 재업로드가 복사할 것이 없어 실패하고, 업로더는 원본을 깨진
/// 것으로 기록하는데 리졸버는 캐시 적중을 계속 반환하므로, 그 텍스처는 다시 그려지지 않는다.
/// 바이트 예산이 이미 이 픽셀 값을 청구하므로, 유지하는 쪽이 회계도 정직해진다.
/// </summary>
class D3D12ResolvedTexture final : public IResolvedTexture
{
public:
    /// <summary>
    /// GPU 바인딩 캐시를 위한 안정적인 정체성이다. id는 결코 재사용되지 않으므로, 이 값을 키로
    /// 삼은 바인딩이 퇴거된 주소를 다시 차지한 다른 리소스와 혼동될 일이 없다.
    /// </summary>
    std::uint64_t id = 0;
    /// <summary>픽셀의 revision이다. 업로더는 바인딩의 것과 다르면 같은 텍스처에 다시 올린다.</summary>
    std::uint64_t revision = 0;

    /// <summary>
    /// 업로드할 픽셀이다. 복사되지 않고 그것을 만든 쪽과 공유된다.
    ///
    /// 복사하면 모든 텍스처의 세 번째 사본이 메모리에 생긴다: 에셋이 하나를 쥐고, 프레임이
    /// 같은 것을 나르고, 여기서 또 하나. aliasing 생성자로 에셋 페이로드나 래스터화된 텍스트
    /// 이미지 안의 벡터를 가리키면서 그 객체를 살려 두므로, 몇 프레임이 참조하든 텍스처는 한
    /// 번만 존재한다.
    /// </summary>
    std::shared_ptr<const std::vector<std::byte>> pixels;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT bytesPerPixel = 0;
    UINT width = 0;
    UINT height = 0;

    [[nodiscard]] bool IsValid() const
    {
        return width > 0 && height > 0 && bytesPerPixel > 0 && format != DXGI_FORMAT_UNKNOWN &&
            pixels && pixels->size() == GetPixelByteSize();
    }

    /// <summary>
    /// resolve된 텍스처는 픽셀이 업로드되고 해제된 뒤에도 그릴 수 있는 상태로 남는다. draw에
    /// 필요한 것은 CPU 사본이 아니라 GPU 바인딩이기 때문이다.
    /// </summary>
    [[nodiscard]] bool IsDrawable() const override
    {
        return id != 0 && width > 0 && height > 0 && format != DXGI_FORMAT_UNKNOWN;
    }

    [[nodiscard]] const char* GetBackendName() const override { return "D3D12"; }

    [[nodiscard]] unsigned int GetPixelWidth() const override { return width; }
    [[nodiscard]] unsigned int GetPixelHeight() const override { return height; }

    [[nodiscard]] std::size_t GetPixelByteSize() const
    {
        return static_cast<std::size_t>(width) * height * bytesPerPixel;
    }
};

}
