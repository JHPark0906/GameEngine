#pragma once

#include <cstddef>

#include <d3d12.h>
#include <wrl/client.h>

namespace GameEngine::Rendering::D3D12
{

/// <summary>
/// default 힙 버퍼와 이를 채울 스테이징 버퍼를 준비한다. 복사 명령은 기록하지 않는다.
/// D3D12MeshUploader와 D3D12SkinnedMeshUploader가 같은 준비 경로를 사용한다.
///
/// source는 호출 안에서 복사하지만 upload는 GPU가 나중에 읽는다. 호출자는 반환된 두 리소스를
/// 복사 명령의 펜스가 완료될 때까지 유지해야 하며, 준비 성공은 GPU 완료를 뜻하지 않는다.
/// </summary>
// source는 호출 안에서 복사하지만 upload는 GPU가 나중에 읽는다. 호출자는 반환된 두
// 리소스를 복사 명령의 펜스가 완료될 때까지 유지해야 하며, 성공 반환은 GPU 완료가 아니다.
[[nodiscard]] bool PrepareBufferUpload(
    ID3D12Device& device,
    const void* source,
    std::size_t byteSize,
    Microsoft::WRL::ComPtr<ID3D12Resource>& buffer,
    Microsoft::WRL::ComPtr<ID3D12Resource>& upload);

// Recording cannot fail. Prepare every buffer in a mesh before recording any of its copies,
// and place the resources under frame ownership before calling this function.
void RecordBufferUpload(
    ID3D12GraphicsCommandList& commandList,
    ID3D12Resource& buffer, ID3D12Resource& upload,
    std::size_t byteSize, D3D12_RESOURCE_STATES finalState);

}
