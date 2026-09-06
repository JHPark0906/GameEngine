#include "MeshSubmissionBudgetTests.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Diagnostics/Debug.h"
#include "Math/Matrix.h"
#include "Rendering/MeshDrawGeometry.h"
#include "Rendering/MeshRenderPass.h"
#include "Rendering/RenderCommandList.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Rendering/RenderSubmissionPolicy.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using namespace GameEngine::Rendering;

    class DrawableGeometry final : public IResolvedGeometry
    {
    public:
        bool IsDrawable() const override { return true; }
        const char* GetBackendName() const override { return "BudgetProbe"; }
    };

    class DrawableTexture final : public IResolvedTexture
    {
    public:
        bool IsDrawable() const override { return true; }
        const char* GetBackendName() const override { return "BudgetProbe"; }
        unsigned int GetPixelWidth() const override { return 1; }
        unsigned int GetPixelHeight() const override { return 1; }
    };

    class SubmissionProbe final : public IRenderCommandList
    {
    public:
        const char* GetBackendName() const override { return "BudgetProbe"; }
        ClipSpaceConvention GetClipSpace() const override { return EngineClipSpace; }
        bool DrawMesh(const MeshShading&, const IResolvedGeometry&, const IResolvedTexture&) override
        {
            ++staticDraws;
            return true;
        }
        bool DrawSkinnedMesh(const MeshShading& shading, const IResolvedGeometry&,
            const IResolvedTexture&, std::span<const Math::Matrix4x4>) override
        {
            ++skinnedDraws;
            lastAlphaBlended = shading.alphaBlended;
            return true;
        }
        bool DrawQuad(PipelineKind, const QuadTransform&, const IResolvedTexture&) override
        {
            return true;
        }
        bool DrawQuads(PipelineKind, std::span<const QuadTransform>, const IResolvedTexture&) override
        {
            return true;
        }
        void EndPass(RenderPass) override {}

        std::size_t staticDraws = 0;
        std::size_t skinnedDraws = 0;
        bool lastAlphaBlended = false;
    };
}

bool RunMeshSubmissionBudgetTests()
{
    using TestSupport::Expect;
    RenderFrameBuilder builder;
    CameraRenderData camera;
    camera.view = Math::Matrix4x4::Identity();
    camera.projection = Math::Matrix4x4::Identity();
    builder.SetCamera(camera);
    MeshDraw staticDraw;
    staticDraw.pipeline = builder.AddPipeline({ PipelineKind::Mesh });
    staticDraw.localToWorld = Math::Matrix4x4::Identity();
    SkinnedMeshDraw skinnedDraw;
    skinnedDraw.pipeline = builder.AddPipeline({ PipelineKind::SkinnedMesh });
    skinnedDraw.localToWorld = Math::Matrix4x4::Identity();
    skinnedDraw.boneMatrices = std::make_shared<std::vector<Math::Matrix4x4>>(
        1, Math::Matrix4x4::Identity());
    const RenderFrame frame = std::move(builder).Build();
    MeshRenderPass pass;
    SubmissionProbe probe;
    const DrawableGeometry geometry;
    const DrawableTexture texture;
    const auto submit = [&]
    {
        pass.DrawSkinned(probe, frame, skinnedDraw, geometry, texture);
    };
    const std::size_t budget = FrameSubmissionBudget.maximumSkinnedMeshesPerFrame;
    bool passed = Expect(budget >= 2,
        "the common skinned mesh budget must support multiple animated instances");
    std::size_t budgetWarnings = 0;
    const auto listener = Diagnostics::Debug::AddLogListener(
        [&](const Diagnostics::LogEntry& entry)
        {
            if (entry.message.find("skinned mesh submission budget") != std::string::npos)
            {
                ++budgetWarnings;
            }
        });

    skinnedDraw.tint.a = 0.0f;
    for (std::size_t index = 0; index <= budget; ++index) submit();
    passed &= Expect(probe.skinnedDraws == 0 && budgetWarnings == 0,
        "invisible skinned meshes must not consume GPU submissions or the shared budget");
    skinnedDraw.tint.a = 1.0f;
    for (std::size_t index = 0; index < budget; ++index) submit();
    passed &= Expect(probe.skinnedDraws == budget && !probe.lastAlphaBlended && budgetWarnings == 0,
        "all opaque skinned draws through the exact common limit must be submitted");
    submit();
    submit();
    passed &= Expect(probe.skinnedDraws == budget && budgetWarnings == 1,
        "excess skinned draws must be skipped with one warning for the whole frame");
    pass.Draw(probe, frame, staticDraw, geometry, texture);
    passed &= Expect(probe.staticDraws == 1,
        "exhausting the bone budget must not suppress ordinary mesh draws");

    pass.BeginFrame();
    skinnedDraw.tint.a = 0.5f;
    submit();
    passed &= Expect(probe.skinnedDraws == budget + 1 && probe.lastAlphaBlended,
        "a new frame must restore budget and select the shared skinned fade mode");
    for (std::size_t index = 1; index < budget; ++index) submit();
    submit();
    passed &= Expect(probe.skinnedDraws == 2 * budget && budgetWarnings == 2,
        "the next frame must enforce the same limit and report its own first overflow");
    Diagnostics::Debug::RemoveLogListener(listener);
    return passed;
}

static const TestSupport::Registration gMeshSubmissionBudgetTests{
    "RenderFrame", "shared skinned mesh submission budget tests should pass",
    RunMeshSubmissionBudgetTests };
