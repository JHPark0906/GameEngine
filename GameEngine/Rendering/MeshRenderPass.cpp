#include "pch.h"
#include "MeshRenderPass.h"

#include "ClipSpace.h"
#include "MeshDrawGeometry.h"
#include "RenderColorPolicy.h"
#include "RenderSubmissionPolicy.h"
#include "../Diagnostics/Debug.h"

#include <optional>

namespace GameEngine::Rendering
{

void MeshRenderPass::BeginFrame()
{
    mSkinnedMeshCount = 0;
    mSkinnedBudgetReported = false;
}

void MeshRenderPass::Draw(
    IRenderCommandList& commandList,
    const RenderFrame& frame,
    const MeshDraw& draw,
    const IResolvedGeometry& geometry,
    const IResolvedTexture& material) const
{
    if (!geometry.IsDrawable() || !material.IsDrawable() || draw.tint.a <= 0.0f)
    {
        return;
    }

    // The frame's pipeline handle is authoritative. Validation already proved the handle resolves
    // and matches the payload, so a mismatch here means the caller passed a packet this pass does
    // not own rather than that the frame was malformed.
    const Pipeline* const pipeline = frame.GetPipeline(draw.pipeline);
    if (!pipeline || pipeline->kind != PipelineKind::Mesh)
    {
        return;
    }

    std::optional<MeshShading> transform =
        TryBuildMeshShading(frame, draw, commandList.GetBackendName());
    if (!transform)
    {
        return;
    }
    transform->tint = ToLinearColor(draw.tint);
    transform->alphaBlended = draw.UsesAlphaBlending();
    ApplyClipSpaceCorrection(transform->worldViewProjection, commandList.GetClipSpace());

    static_cast<void>(commandList.DrawMesh(*transform, geometry, material));
}

void MeshRenderPass::DrawSkinned(
    IRenderCommandList& commandList,
    const RenderFrame& frame,
    const SkinnedMeshDraw& draw,
    const IResolvedGeometry& geometry,
    const IResolvedTexture& material)
{
    if (!geometry.IsDrawable() || !material.IsDrawable() || draw.tint.a <= 0.0f || !draw.boneMatrices ||
        draw.boneMatrices->empty())
    {
        return;
    }

    const Pipeline* const pipeline = frame.GetPipeline(draw.pipeline);
    if (!pipeline || pipeline->kind != PipelineKind::SkinnedMesh)
    {
        return;
    }

    std::optional<MeshShading> transform =
        TryBuildMeshShading(frame, draw.localToWorld, commandList.GetBackendName());
    if (!transform)
    {
        return;
    }
    transform->tint = ToLinearColor(draw.tint);
    transform->alphaBlended = draw.UsesAlphaBlending();
    ApplyClipSpaceCorrection(transform->worldViewProjection, commandList.GetClipSpace());

    // Match the bone-constant storage budget before either backend submits the draw.
    // Count only valid draws and report overflow once, even if many remaining objects exceed it.
    if (mSkinnedMeshCount >= FrameSubmissionBudget.maximumSkinnedMeshesPerFrame)
    {
        if (!mSkinnedBudgetReported)
        {
            Diagnostics::Debug::LogWarning("The skinned mesh submission budget is full. maximum=",
                FrameSubmissionBudget.maximumSkinnedMeshesPerFrame,
                "; additional skinned meshes are skipped for this frame.");
            mSkinnedBudgetReported = true;
        }
        return;
    }
    ++mSkinnedMeshCount;
    static_cast<void>(
        commandList.DrawSkinnedMesh(*transform, geometry, material, *draw.boneMatrices));
}

}
