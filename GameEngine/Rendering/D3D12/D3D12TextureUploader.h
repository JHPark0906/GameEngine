#pragma once


#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <wrl/client.h>

#include "D3D12DescriptorHeapAllocator.h"
#include "D3D12FrameResources.h"

namespace GameEngine::Rendering::D3D12
{

class D3D12ResolvedTexture;

/// <summary>resolve된 텍스처 원본 하나에 바인딩된 GPU 텍스처와 shader-visible descriptor 슬롯이다.</summary>
struct D3D12TextureBinding
{
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    D3D12DescriptorAllocation descriptor;
    /// <summary>텍스처에 마지막으로 올린 픽셀의 revision이다.</summary>
    std::uint64_t revision = 0;
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

/// <summary>
/// command list에 의존하는 텍스처 업로드와 descriptor 바인딩을 소유한다. 바인딩 캐시는
/// descriptor 힙 용량으로 제한되고 각 바인딩이 자기 descriptor 슬롯을 소유하므로, 바인딩을
/// 퇴거하면 힙을 소진하는 대신 다음 업로드를 위한 슬롯이 풀려난다.
/// </summary>
class D3D12TextureUploader final
{
public:
    D3D12TextureUploader();
    ~D3D12TextureUploader();

    D3D12TextureUploader(const D3D12TextureUploader&) = delete;
    D3D12TextureUploader& operator=(const D3D12TextureUploader&) = delete;

    /// <summary>
    /// 이 업로더를 장치와 shader-visible 힙에 묶는다. 바인딩 캐시의 항목 상한은 힙의 실제
    /// 슬롯 수이며, 힙에 슬롯이 없으면 실패한다.
    /// </summary>
    [[nodiscard]] bool Initialize(
        ID3D12Device& device, D3D12DescriptorHeapAllocator& descriptorAllocator);

    /// <summary>
    /// 바인딩 사용 수명을 시작하고 완료된 업로드 버퍼를 재사용한다. 호출자는 선택한 슬롯의
    /// 이전 제출이 펜스까지 완료됐음을 보증하고, 모든 기록을 BeginFrame 순서로 같은 큐에
    /// 제출한다. 풀의 보존 상한과 관계없이 GPU가 아직 참조하는 버퍼는 완료까지 살아 있다.
    /// </summary>
    /// <param name="frameIndex">GPU 완료를 확인한 기록 슬롯이다.</param>
    /// <param name="advanceCache">
    /// 바인딩과 업로드 캐시의 프레임을 올릴지다. 같은 엔진 프레임의 추가 캡처는 올리지 않는다.
    /// </param>
    void BeginFrame(UINT frameIndex, bool advanceCache = true);

    /// <summary>
    /// resolve된 텍스처의 GPU 바인딩을 반환하며, 처음 쓰일 때 업로드한다. 업로드가 실패하거나
    /// descriptor 힙에 이번 프레임에 쓸 수 있는 슬롯이 없으면 null을 반환한다.
    /// </summary>
    [[nodiscard]] const D3D12TextureBinding* Resolve(
        ID3D12GraphicsCommandList& commandList, const D3D12ResolvedTexture& source);

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

}
