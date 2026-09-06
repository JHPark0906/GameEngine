#pragma once

#include <cstddef>
#include <memory>

#include <d3d12.h>
#include <wrl/client.h>

namespace GameEngine::Assets
{
struct MeshData;
}

namespace GameEngine::Rendering::D3D12
{

/// <summary>메시 하나에 바인딩된 정점·인덱스 버퍼와 draw에 필요한 뷰들이다.</summary>
struct D3D12MeshBinding
{
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
    D3D12_INDEX_BUFFER_VIEW indexBufferView{};
    UINT indexCount = 0;
};

/// <summary>
/// command list에 의존하는 메시 업로드를 소유한다.
///
/// 메시 버퍼는 텍스처와 같은 배치로 올린다 — default 힙 리소스, 임시 스테이징 버퍼, 복사.
/// 업로드 힙에 만들어 그대로 두면 모든 draw가 메시의 수명 내내 CPU가 쓸 수 있는 메모리에서
/// 버스를 건너 정점을 읽는다. 리소스 리졸버가 아니라 여기 있어야 하는 이유는 복사에 command
/// list가 필요한데 리졸버는 그것을 요구해선 안 되기 때문이다.
/// </summary>
class D3D12MeshUploader final
{
public:
    D3D12MeshUploader();
    ~D3D12MeshUploader();

    D3D12MeshUploader(const D3D12MeshUploader&) = delete;
    D3D12MeshUploader& operator=(const D3D12MeshUploader&) = delete;

    void Initialize(ID3D12Device& device);

    /// <summary>
    /// 바인딩 사용 수명을 시작한다. 호출자는 슬롯의 이전 제출 펜스를 기다려야 한다.
    /// 그 제출과 같은 큐의 앞선 제출이 소유한 스테이징 버퍼를 함께 해제한다.
    /// </summary>
    /// <param name="advanceCache">바인딩 캐시의 프레임을 올릴지다. 캡처는 올리지 않는다.</param>
    void BeginFrame(UINT frameIndex, bool advanceCache = true);

    /// <summary>완료가 확인되지 않은 제출이 소유하는 업로드 자원 수다.</summary>
    [[nodiscard]] std::size_t GetPendingUploadCount() const;

    /// <summary>
    /// 메시의 GPU 바인딩을 반환하며, 처음 쓰일 때 업로드한다. 업로드가 실패하거나 캐시가 이번
    /// 프레임에 자리를 만들 수 없으면 null을 반환한다.
    /// </summary>
    [[nodiscard]] const D3D12MeshBinding* Resolve(
        ID3D12GraphicsCommandList& commandList, const Assets::MeshData& mesh);

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

}
