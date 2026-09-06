#include "MeshFadeDepthTests.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "Assets/MeshData.h"
#include "Assets/SkinnedMeshData.h"
#include "BackendPixelSupport.h"
#include "Core/ResourceId.h"
#include "Math/Color.h"
#include "Math/Matrix.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderColorPolicy.h"
#include "Rendering/RenderFrameBuilder.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using TestSupport::Expect;
    constexpr unsigned int ImageSize = 128;
    constexpr Math::Color ClearColor{ .1f, .2f, .6f, 1.0f };
    constexpr Math::Color NearColor{ .75f, .125f, .25f, .5f };
    constexpr Math::Color FarColor{ .125f, .75f, .5f, .5f };
    constexpr Math::Color OpaqueColor{ .125f, .75f, .5f, 1.0f };
    constexpr Math::Color BehindColor{ .2f, .3f, .8f, .5f };

    struct Layer
    {
        float depth;
        Math::Color tint;
        Rendering::RenderPass pass = Rendering::RenderPass::Transparent;
    };

    struct Scenario
    {
        const char* name;
        std::vector<Layer> layers;
        Math::Color expected;
    };

    Math::Color Blend(const Math::Color source, const Math::Color destination)
    {
        const Math::Color sourceLinear = Rendering::ToLinearColor(source);
        const Math::Color destinationLinear = Rendering::ToLinearColor(destination);
        const float inverseAlpha = 1.0f - source.a;
        return {
            Rendering::LinearChannelToSrgb(sourceLinear.r * source.a + destinationLinear.r * inverseAlpha),
            Rendering::LinearChannelToSrgb(sourceLinear.g * source.a + destinationLinear.g * inverseAlpha),
            Rendering::LinearChannelToSrgb(sourceLinear.b * source.a + destinationLinear.b * inverseAlpha),
            source.a + destination.a * inverseAlpha };
    }

    struct Fixtures
    {
        std::shared_ptr<Assets::MeshData> mesh = std::make_shared<Assets::MeshData>();
        std::shared_ptr<Assets::SkinnedMeshData> skinned = std::make_shared<Assets::SkinnedMeshData>();
        std::shared_ptr<const Assets::TextureData> white = TestSupport::MakeHalvedTexture(
            Core::MakeResourceId(Core::ResourceIdDomain::Texture, 0xFADED),
            { 255, 255, 255, 255 }, { 255, 255, 255, 255 });
        std::shared_ptr<const std::vector<Math::Matrix4x4>> bones =
            std::make_shared<std::vector<Math::Matrix4x4>>(1, Math::Matrix4x4::Identity());

        Fixtures()
        {
            mesh->id = Core::MakeResourceId(Core::ResourceIdDomain::Mesh, 0xFADED);
            skinned->id = Core::MakeResourceId(Core::ResourceIdDomain::SkinnedMesh, 0xFADED);
            const std::array corners{ std::array{ -1.0f, -1.0f }, std::array{ -1.0f, 1.0f },
                std::array{ 1.0f, 1.0f }, std::array{ 1.0f, -1.0f } };
            for (const auto& corner : corners)
            {
                Core::MeshVertex vertex;
                vertex.position = { corner[0], corner[1], 0.0f };
                vertex.normal = { 0, 0, -1 };
                vertex.textureCoordinate = { 0, 0 };
                mesh->vertices.push_back(vertex);
                Core::SkinnedMeshVertex skinnedVertex;
                skinnedVertex.position = vertex.position;
                skinnedVertex.normal = vertex.normal;
                skinnedVertex.textureCoordinate = vertex.textureCoordinate;
                skinnedVertex.boneIndices = { 0, 0, 0, 0 };
                skinnedVertex.boneWeights = { 1, 0, 0, 0 };
                skinned->vertices.push_back(skinnedVertex);
            }
            mesh->indices = { 0, 1, 2, 0, 2, 3 };
            mesh->bounds = Assets::ComputeBounds(mesh->vertices);
            skinned->indices = mesh->indices;
            skinned->bounds = Assets::ComputeSkinnedBounds(skinned->vertices);
        }
    };

    Rendering::RenderFrame BuildFrame(const Fixtures& fixtures, const bool skinned,
        const float scale, const std::span<const Layer> layers)
    {
        Rendering::RenderFrameBuilder builder;
        const auto extent = static_cast<unsigned int>(ImageSize * scale);
        builder.SetRenderTargetSize({ extent, extent });
        Rendering::CameraRenderData camera;
        camera.clearColor = ClearColor;
        camera.view = Math::Matrix4x4::Identity();
        camera.projection = Math::Matrix4x4::CreateOrthographicLeftHanded(4, 4, .1f, 10);
        builder.SetCamera(camera);
        builder.AddAmbientLight(Math::Color::White);
        const auto material = builder.AddMaterial({ fixtures.white });
        for (std::size_t index = 0; index < layers.size(); ++index)
        {
            const Layer& layer = layers[index];
            const auto transform = Math::Matrix4x4::CreateTranslation({ 0, 0, layer.depth });
            const int order = static_cast<int>(index);
            const unsigned int instanceId = static_cast<unsigned int>(index + 1);
            if (skinned)
            {
                Rendering::SkinnedMeshDraw draw;
                draw.pipeline = builder.AddPipeline({ Rendering::PipelineKind::SkinnedMesh });
                draw.geometry = builder.AddSkinnedGeometry({ fixtures.skinned });
                draw.material = material;
                draw.localToWorld = transform;
                draw.boneMatrices = fixtures.bones;
                draw.tint = layer.tint;
                static_cast<void>(builder.TryAddDraw(layer.pass, std::move(draw), order, instanceId));
            }
            else
            {
                Rendering::MeshDraw draw;
                draw.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Mesh });
                draw.geometry = builder.AddGeometry({ fixtures.mesh });
                draw.material = material;
                draw.localToWorld = transform;
                draw.tint = layer.tint;
                static_cast<void>(builder.TryAddDraw(layer.pass, std::move(draw), order, instanceId));
            }
        }
        return std::move(builder).Build();
    }

    bool PixelNear(const TestSupport::Rgba actual, const TestSupport::Rgba expected)
    {
        return std::abs(int(actual.r) - int(expected.r)) <= 2 &&
            std::abs(int(actual.g) - int(expected.g)) <= 2 &&
            std::abs(int(actual.b) - int(expected.b)) <= 2 &&
            std::abs(int(actual.a) - int(expected.a)) <= 2;
    }
}

bool RunMeshFadeDepthTests()
{
    using namespace GameEngine;
    const Fixtures fixtures;
    const std::array scenarios{
        Scenario{ "near fade followed by far fade",
            { { 1, NearColor }, { 2, FarColor } }, Blend(FarColor, Blend(NearColor, ClearColor)) },
        Scenario{ "opaque foreground occludes a farther fade",
            { { 1, OpaqueColor, Rendering::RenderPass::Opaque }, { 2, NearColor } }, OpaqueColor },
        // All three share one pipeline and pass. The far opaque draw must cover the earlier
        // near fade, then write depth so that the final, still farther fade cannot cover it.
        Scenario{ "fade to opaque to fade restores depth writes",
            { { 1, NearColor }, { 2, OpaqueColor }, { 3, BehindColor } }, OpaqueColor } };
    const auto backends = TestSupport::SupportedBackends();
    bool passed = Expect(!backends.empty(), "mesh fade depth checks require a graphics backend");
    for (const auto* backend : backends)
    {
        auto device = backend->CreateDevice();
        if (!Expect(device && device->Initialize(Platform::NativeSurface{}),
                "the fade depth graphics device must initialize"))
        {
            passed = false;
            continue;
        }
        passed &= TestSupport::ForEachUiScale([&](const float scale)
        {
            bool scalePassed = true;
            for (const bool skinned : { false, true })
            {
                for (const Scenario& scenario : scenarios)
                {
                    const auto frame = BuildFrame(fixtures, skinned, scale, scenario.layers);
                    const auto& transparent = frame.GetDrawPackets(Rendering::RenderPass::Transparent);
                    if (transparent.size() > 1)
                    {
                        scalePassed &= Expect(transparent[0].sortingOrder < transparent[1].sortingOrder &&
                            transparent[0].viewDepth < transparent[1].viewDepth,
                            "the fixture must force near-before-far through sorting order");
                    }
                    Rendering::CapturedImage image;
                    if (!Expect(device->RenderToImage(frame, image) && image.IsValid(),
                            "overlapping meshes must render into a readable image"))
                    {
                        return false;
                    }
                    const auto sample = static_cast<unsigned int>((ImageSize / 2) * scale);
                    const auto actual = TestSupport::ReadPixel(image, sample, sample);
                    const auto expected = TestSupport::Quantize(scenario.expected);
                    const bool matches = PixelNear(actual, expected);
                    if (!matches)
                    {
                        std::cerr << backend->id << (skinned ? " skinned " : " static ") <<
                            scenario.name << " actual=" << TestSupport::Describe(actual) <<
                            " expected=" << TestSupport::Describe(expected) << '\n';
                    }
                    scalePassed &= Expect(matches,
                        "mesh fading must disable depth writes while preserving opaque occlusion");
                }
            }
            return scalePassed;
        });
    }
    return passed;
}

static const TestSupport::Registration gMeshFadeDepthTests{
    "BackendImage", "static and skinned mesh fade depth tests should pass", RunMeshFadeDepthTests };
