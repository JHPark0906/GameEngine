#include "RenderSubmissionRegressionTests.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "Math/Matrix.h"
#include "Rendering/D3D12/D3D12FrameResources.h"
#include "Rendering/MeshDrawGeometry.h"
#include "Rendering/RenderCommandList.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Rendering/RenderSubmissionPolicy.h"
#include "Rendering/SpriteRenderPass.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using namespace GameEngine::Rendering;

    class Texture final : public IResolvedTexture
    {
    public:
        bool IsDrawable() const override { return true; }
        const char* GetBackendName() const override { return "SubmissionProbe"; }
        unsigned int GetPixelWidth() const override { return 8; }
        unsigned int GetPixelHeight() const override { return 8; }
    };

    class Probe final : public IRenderCommandList
    {
    public:
        const char* GetBackendName() const override { return "SubmissionProbe"; }
        ClipSpaceConvention GetClipSpace() const override { return EngineClipSpace; }
        bool DrawMesh(const MeshShading&, const IResolvedGeometry&, const IResolvedTexture&) override { return true; }
        bool DrawSkinnedMesh(const MeshShading&, const IResolvedGeometry&, const IResolvedTexture&,
            std::span<const Math::Matrix4x4>) override { return true; }
        bool DrawQuad(PipelineKind, const QuadTransform&, const IResolvedTexture&) override
        {
            ++quads;
            return true;
        }
        bool DrawQuads(PipelineKind, const std::span<const QuadTransform> transforms,
            const IResolvedTexture&) override
        {
            quads += transforms.size();
            return true;
        }
        void EndPass(RenderPass) override {}
        std::size_t quads = 0;
    };
}

bool RunRenderSubmissionRegressionTests()
{
    using TestSupport::Expect;
    RenderFrameBuilder builder;
    builder.SetCamera({});
    builder.SetRenderTargetSize({ 128, 128 });
    const auto spritePipeline = builder.AddPipeline({ PipelineKind::Sprite });
    const auto textPipeline = builder.AddPipeline({ PipelineKind::Text });
    const RenderFrame frame = std::move(builder).Build();
    const Texture texture;
    Probe probe;
    SpriteRenderPass pass;
    const std::size_t budget = FrameSubmissionBudget.maximumQuadsPerFrame;

    TilemapDraw tilemap;
    tilemap.pipeline = spritePipeline;
    tilemap.tiles = std::make_shared<std::vector<TilemapTile>>(budget - 5);
    pass.DrawTilemap(probe, frame, tilemap, texture);
    bool passed = Expect(probe.quads == budget - 5, "the tile batch consumes its actual number of quads");

    SpriteDraw sliced;
    sliced.pipeline = spritePipeline;
    sliced.sliced = true;
    sliced.border = { 1, 1, 1, 1 };
    sliced.size = { 12, 12 };
    pass.DrawSprite(probe, frame, sliced, texture);
    passed &= Expect(probe.quads == budget,
        "a nine-slice crossing the limit submits only the remaining five quads");
    TextDraw text;
    text.pipeline = textPipeline;
    text.glyphs = std::make_shared<std::vector<TextGlyphQuad>>(2);
    pass.DrawTextQuad(probe, frame, text, texture);
    passed &= Expect(probe.quads == budget, "text shares the tile and sprite budget across packets");
    pass.BeginFrame();
    pass.DrawTextQuad(probe, frame, text, texture);
    passed &= Expect(probe.quads == budget + 2, "a new frame restores text submission capacity");

    RenderFrameBuilder storageBuilder;
    TilemapDraw fullMap;
    fullMap.tiles = std::make_shared<std::vector<TilemapTile>>(budget + 10);
    static_cast<void>(storageBuilder.TryAddDraw(RenderPass::Transparent, fullMap));
    // Static meshes retain the existing unbounded contract. Their constants must be independent
    // of the quad limit, even when a mesh-only frame is larger than that limit.
    for (std::size_t index = 0; index < budget + 1; ++index)
        static_cast<void>(storageBuilder.TryAddDraw(RenderPass::Opaque, MeshDraw{}));
    const auto storageFrame = std::move(storageBuilder).Build();
    passed &= Expect(D3D12::ComputeFrameConstantBytes(storageFrame) ==
        budget * 256 + (budget + 1) * 512,
        "constant storage covers the quad budget plus every mesh at its own CBV alignment");

    const auto world = Math::Matrix4x4::CreateScale({ 2, 1, 1 }) *
        Math::Matrix4x4::CreateRotationZDegrees(37);
    const auto shading = TryBuildMeshShading(frame, world, "normal probe");
    if (!Expect(shading.has_value(), "a nonuniform transform produces mesh shading")) return false;
    const auto tangent = world.TransformDirection({ 1, -1, 0 });
    const auto normal = shading->normalToWorld.TransformDirection({ 1, 1, 0 });
    passed &= Expect(std::abs(tangent.Dot(normal)) < 0.0001f,
        "the transformed normal stays perpendicular to a transformed surface tangent");
    const auto flattened = TryBuildMeshShading(frame, Math::Matrix4x4::CreateScale({ 1, 1, 0 }), "normal probe");
    passed &= Expect(flattened && flattened->normalToWorld.TransformDirection({ 0, 0, -1 }).GetZ() < 0,
        "flattening the unused axis preserves a planar mesh and its surface normal");
    return passed;
}

static const TestSupport::Registration gRenderSubmissionRegressionTests{
    "RenderFrame", "quad budgets, mesh constant capacity, and normal transforms remain correct",
    RunRenderSubmissionRegressionTests };
