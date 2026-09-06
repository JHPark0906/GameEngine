#include "pch.h"
#include "SpriteRenderPass.h"

#include <vector>

#include "ClipSpace.h"
#include "QuadDrawGeometry.h"
#include "RenderSubmissionPolicy.h"
#include "../Diagnostics/Debug.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

namespace GameEngine::Rendering
{

void SpriteRenderPass::BeginFrame()
{
    mSubmittedQuads = 0;
    mBudgetWarningReported = false;
}

void SpriteRenderPass::SubmitQuads(IRenderCommandList& commandList, const PipelineKind kind,
    const std::span<const QuadTransform> quads, const IResolvedTexture& texture) const
{
    const std::size_t accepted = (std::min)(
        quads.size(), FrameSubmissionBudget.maximumQuadsPerFrame - mSubmittedQuads);
    if (accepted < quads.size() && !mBudgetWarningReported)
    {
        Diagnostics::Debug::LogWarning("The quad submission budget is full. maximum=",
            FrameSubmissionBudget.maximumQuadsPerFrame,
            "; additional quads are skipped for this frame.");
        mBudgetWarningReported = true;
    }
    mSubmittedQuads += accepted;
    if (accepted != 0) static_cast<void>(commandList.DrawQuads(kind, quads.first(accepted), texture));
}

void SpriteRenderPass::DrawSprite(
    IRenderCommandList& commandList,
    const RenderFrame& frame,
    const SpriteDraw& draw,
    const IResolvedTexture& material) const
{
    if (!material.IsDrawable())
    {
        return;
    }

    const Pipeline* const pipeline = frame.GetPipeline(draw.pipeline);
    if (!pipeline || pipeline->kind != PipelineKind::Sprite)
    {
        return;
    }

    // A sliced sprite is up to nine of the same quad the simple path draws, placed by the shared
    // geometry. The command list cannot tell the difference, which is the point: slicing exists
    // once, and a new backend gets it for free.
    if (draw.sliced)
    {
        std::array<QuadTransform, 9> quads;
        const std::size_t count = BuildSlicedSpriteQuads(
            frame, draw, material.GetQuadTextureSize(), commandList.GetBackendName(), quads);
        for (std::size_t index = 0; index < count; ++index)
        {
            ApplyClipSpaceCorrection(quads[index].worldViewProjection, commandList.GetClipSpace());
        }
        SubmitQuads(commandList, PipelineKind::Sprite, std::span{ quads }.first(count), material);
        return;
    }

    std::optional<QuadTransform> quad = TryBuildSpriteQuad(
        frame, draw, material.GetQuadTextureSize(), commandList.GetBackendName());
    if (!quad)
    {
        return;
    }
    ApplyClipSpaceCorrection(quad->worldViewProjection, commandList.GetClipSpace());

    SubmitQuads(commandList, PipelineKind::Sprite, std::span{ &*quad, 1 }, material);
}

void SpriteRenderPass::DrawTextQuad(
    IRenderCommandList& commandList,
    const RenderFrame& frame,
    const TextDraw& draw,
    const IResolvedTexture& text) const
{
    if (!text.IsDrawable() || !draw.glyphs)
    {
        return;
    }

    const Pipeline* const pipeline = frame.GetPipeline(draw.pipeline);
    if (!pipeline || pipeline->kind != PipelineKind::Text)
    {
        return;
    }

    // 글리프마다 quad 하나다: nine-slice가 최대 아홉 개를 그리듯 텍스트는 글리프 수만큼
    // 그리고, 전부 같은 아틀라스 페이지 텍스처를 본다 — 백엔드에 글리프라는 개념은 없다.
    const std::optional<TextQuadBasis> basis =
        TryBuildTextQuadBasis(frame, draw, commandList.GetBackendName());
    if (!basis)
    {
        return;
    }
    // 한 줄의 글리프는 같은 아틀라스 페이지를 보는 하나의 배치다. 프레임이 그것을 패킷
    // 하나로 나르므로 제출도 한 번이어야 한다 — 글리프마다 호출로 흩어지면 백엔드는
    // 배치를 볼 기회가 없다.
    std::vector<QuadTransform> quads;
    quads.reserve(draw.glyphs->size());
    for (const TextGlyphQuad& glyph : *draw.glyphs)
    {
        QuadTransform quad = PlaceTextGlyphQuad(*basis, draw, glyph);
        ApplyClipSpaceCorrection(quad.worldViewProjection, commandList.GetClipSpace());
        quads.push_back(quad);
    }
    SubmitQuads(commandList, PipelineKind::Text, quads, text);
}


void SpriteRenderPass::DrawTilemap(
    IRenderCommandList& commandList,
    const RenderFrame& frame,
    const TilemapDraw& draw,
    const IResolvedTexture& tileset) const
{
    if (!tileset.IsDrawable() || !draw.tiles)
    {
        return;
    }

    const Pipeline* const pipeline = frame.GetPipeline(draw.pipeline);
    if (!pipeline || pipeline->kind != PipelineKind::Sprite)
    {
        return;
    }

    const std::optional<Math::Matrix4x4> basis =
        TryBuildTilemapBasis(frame, draw, commandList.GetBackendName());
    if (!basis)
    {
        return;
    }
    // 레이어 하나가 배치 하나다. 칸이 수천이어도 백엔드는 그것을 한 덩어리로 받는다.
    std::vector<QuadTransform> quads;
    quads.reserve(draw.tiles->size());
    for (const TilemapTile& tile : *draw.tiles)
    {
        QuadTransform quad = PlaceTilemapTileQuad(*basis, draw, tile);
        ApplyClipSpaceCorrection(quad.worldViewProjection, commandList.GetClipSpace());
        quads.push_back(quad);
    }
    SubmitQuads(commandList, PipelineKind::Sprite, quads, tileset);
}

}
