#pragma once

#include <span>

#include <memory>

#include <d3d12.h>

#include "../RenderCommandList.h"

namespace GameEngine::Rendering::D3D12
{

class ID3D12GraphicsDevice;
class D3D12FrameResources;
class D3D12MeshUploader;
class D3D12SkinnedMeshUploader;
class D3D12TextureUploader;

/// <summary>
/// 공용 렌더 패스의 draw를 D3D12 command list에 기록한다.
///
/// 파이프라인 상태 객체, quad 정점 버퍼, 그리고 D3D12 모델이 요구하는 프레임 상수·descriptor
/// 바인딩을 여기서 소유한다. resolve된 텍스처의 업로드와 descriptor 슬롯 예약도 여기서
/// 일어나는데, 둘 다 지금 기록 중인 command list를 필요로 하기 때문이다.
/// </summary>
class D3D12CommandList final : public IRenderCommandList
{
public:
    D3D12CommandList();
    ~D3D12CommandList() override;

    [[nodiscard]] bool Initialize(
        ID3D12GraphicsDevice& graphicsDevice,
        D3D12FrameResources& frameResources,
        D3D12MeshUploader& meshUploader,
        D3D12SkinnedMeshUploader& skinnedMeshUploader,
        D3D12TextureUploader& textureUploader);

    /// <summary>
    /// 이 command list가 현재 프레임에 기록 중인 리스트를 가리키게 한다.
    /// </summary>
    /// <param name="frameIndex">
    /// 스킨드 메시의 뼈 상수 링이 자기 세그먼트를 고르는 데 쓰는 프레임 번호다 — 주 프레임과
    /// 지연 캡처 채널마다 다른 번호가 오므로, D3D12FrameResources·D3D12MeshUploader와 같은
    /// 규칙으로 세그먼트가 갈린다.
    /// </param>
    void BeginFrame(UINT frameIndex, ID3D12GraphicsCommandList& commandList);

    [[nodiscard]] const char* GetBackendName() const override;

    /// <summary>Direct3D의 클립 공간은 엔진과 일치하므로 아무것도 보정하지 않는다.</summary>
    [[nodiscard]] ClipSpaceConvention GetClipSpace() const override { return EngineClipSpace; }

    [[nodiscard]] bool DrawMesh(
        const MeshShading& transform,
        const IResolvedGeometry& geometry,
        const IResolvedTexture& material) override;

    [[nodiscard]] bool DrawSkinnedMesh(
        const MeshShading& transform,
        const IResolvedGeometry& geometry,
        const IResolvedTexture& material,
        std::span<const Math::Matrix4x4> boneMatrices) override;

    [[nodiscard]] bool DrawQuad(
        PipelineKind pipelineKind,
        const QuadTransform& transform,
        const IResolvedTexture& texture) override;

    /// <summary>같은 텍스처를 보는 quad 배치를 기록한다. 전부 기록했으면 true이다.</summary>
    /// <param name="pipelineKind">이 배치가 쓸 파이프라인 종류이다.</param>
    /// <param name="transforms">그릴 quad들이다.</param>
    /// <param name="texture">배치 전체가 함께 보는 텍스처이다.</param>
    /// <returns>배치 전체를 기록했으면 true이다.</returns>
    [[nodiscard]] bool DrawQuads(
        PipelineKind pipelineKind,
        std::span<const QuadTransform> transforms,
        const IResolvedTexture& texture) override;

    void EndPass(RenderPass pass) override;

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

}
