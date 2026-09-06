#pragma once

#include <cstddef>
#include <memory>

#include <d3d12.h>
#include <wrl/client.h>

namespace GameEngine::Assets
{
struct SkinnedMeshData;
}

namespace GameEngine::Rendering::D3D12
{

/// <summary>스킨드 메시 하나에 바인딩된 정점·인덱스 버퍼와 draw에 필요한 뷰들이다.
/// <see cref="D3D12MeshBinding"/>과 같은 모양이며, 정점 형식이 다를 뿐이다.</summary>
struct D3D12SkinnedMeshBinding
{
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
    D3D12_INDEX_BUFFER_VIEW indexBufferView{};
    UINT indexCount = 0;
};

/// <summary>
/// command list에 의존하는 스킨드 메시 업로드를 소유한다. <see cref="D3D12MeshUploader"/>와 같은
/// 이유로 리소스 리졸버가 아니라 여기 있다 — 복사에 command list가 필요한데 리졸버는 그것을
/// 요구해선 안 된다.
///
/// <c>D3D12MeshUploader</c>를 템플릿으로 만들어 공유하는 대신 나란한 별개 클래스로 둔 이유는
/// 이 파일 대부분(버퍼 업로드·캐시·failedMeshes 회계)이 <c>Assets::MeshData</c>를 몰라도 되게
/// 짜여 있어도, 이 프로젝트에서 백엔드×리소스 종류 하나마다 자기 API의 결로 하나씩 두는 것이
/// 이미 서 있는 관례이기 때문이다 — D3D11ResolvedGeometry와 D3D12ResolvedGeometry가 완전히
/// 다른 모양인 것과 같다.
/// </summary>
class D3D12SkinnedMeshUploader final
{
public:
    D3D12SkinnedMeshUploader();
    ~D3D12SkinnedMeshUploader();

    D3D12SkinnedMeshUploader(const D3D12SkinnedMeshUploader&) = delete;
    D3D12SkinnedMeshUploader& operator=(const D3D12SkinnedMeshUploader&) = delete;

    void Initialize(ID3D12Device& device);

    /// <param name="advanceCache">바인딩 캐시의 프레임을 올릴지다. 캡처는 올리지 않는다.</param>
    void BeginFrame(UINT frameIndex, bool advanceCache = true);

    /// <summary>완료가 확인되지 않은 제출이 소유하는 업로드 자원 수다.</summary>
    [[nodiscard]] std::size_t GetPendingUploadCount() const;

    /// <summary>
    /// 스킨드 메시의 GPU 바인딩을 반환하며, 처음 쓰일 때 업로드한다. 업로드가 실패하거나 캐시가
    /// 이번 프레임에 자리를 만들 수 없으면 null을 반환한다.
    /// </summary>
    [[nodiscard]] const D3D12SkinnedMeshBinding* Resolve(
        ID3D12GraphicsCommandList& commandList, const Assets::SkinnedMeshData& mesh);

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

}
