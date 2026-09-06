#pragma once

#include <span>

#include <memory>

#include "../RenderCommandList.h"

namespace GameEngine::Rendering::D3D11
{

class ID3D11GraphicsDevice;

/// <summary>
/// 공용 렌더 패스의 draw를 D3D11 immediate context를 통해 기록한다.
///
/// 메시와 quad 파이프라인에 필요한 모든 것 — 셰이더, 입력 레이아웃, 상수 버퍼, 그리고 샘플러·
/// 블렌드·래스터라이저·깊이 스텐실 상태 — 을 여기서 소유한다. 그것들이 패스에서 진짜로 API
/// 특유의 부분이라서 여기 살고, 패스 자체는 Rendering에 한 번만 산다.
/// </summary>
class D3D11CommandList final : public IRenderCommandList
{
public:
    D3D11CommandList();
    ~D3D11CommandList() override;

    [[nodiscard]] bool Initialize(ID3D11GraphicsDevice& graphicsDevice);

    /// <summary>
    /// 어느 파이프라인이 바인딩됐는지 잊는다. device context는 프레임 사이에 상태를 유지하지만,
    /// 다른 프레임이 이 command list가 마지막으로 설정한 대로 두고 갔다는 보장은 없다.
    /// </summary>
    void BeginFrame();

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
