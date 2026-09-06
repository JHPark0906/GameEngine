#pragma once

#include "RenderCommandList.h"
#include "RenderFrame.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 프레임의 sprite와 text payload를 그린다. 둘 다 QuadDrawGeometry가 배치하는 텍스처 입힌
/// quad라서 패스를 공유하며, 프레임이 요구하는 파이프라인과 quad의 UV 변환이 flip을 싣는지만
/// 다르다. MeshRenderPass처럼 모든 그래픽 API를 통틀어 하나만 존재한다.
/// </summary>
class SpriteRenderPass final
{
public:
    void BeginFrame();
    /// <summary>스프라이트 draw 하나를 기록하거나, 프레임이 quad를 기술할 수 없으면 건너뛴다.</summary>
    void DrawSprite(
        IRenderCommandList& commandList,
        const RenderFrame& frame,
        const SpriteDraw& draw,
        const IResolvedTexture& material) const;

    /// <summary>
    /// 텍스트 draw 하나를 기록하거나, 프레임이 quad를 기술할 수 없으면 건너뛴다. windows.h가
    /// DrawText를 DrawTextW 매크로로 정의하기 때문에 DrawText라 부르지 않는다. 이미지는
    /// 프론트엔드가 래스터화했으므로, 여기서는 완성된 픽셀을 배치할 뿐 폰트를 참조하지 않는다.
    /// </summary>
    void DrawTextQuad(
        IRenderCommandList& commandList,
        const RenderFrame& frame,
        const TextDraw& draw,
        const IResolvedTexture& text) const;

    /// <summary>
    /// 타일맵 레이어 하나를 기록한다: 같은 타일셋으로 칸 수만큼의 quad를 그린다. 한 패킷이
    /// 여러 quad가 되는 것은 nine-slice와 아틀라스 텍스트가 이미 하는 일이라, 명령 목록에는
    /// 새로울 것이 없다.
    /// </summary>
    void DrawTilemap(
        IRenderCommandList& commandList,
        const RenderFrame& frame,
        const TilemapDraw& draw,
        const IResolvedTexture& tileset) const;

private:
    void SubmitQuads(IRenderCommandList& commandList, PipelineKind kind,
        std::span<const QuadTransform> quads, const IResolvedTexture& texture) const;
    // Drawing is const with respect to placement; these only account for submission.
    mutable std::size_t mSubmittedQuads = 0;
    mutable bool mBudgetWarningReported = false;
};

}
