#include "RenderingStabilityPixelTests.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "Assets/MeshData.h"
#include "Assets/SkinnedMeshData.h"
#include "Assets/TextureData.h"
#include "Math/Matrix.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderColorPolicy.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Rendering/RenderResourceCachePolicy.h"
#include "Rendering/RenderSubmissionPolicy.h"
#include "BackendPixelSupport.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using namespace GameEngine::Rendering;
    using TestSupport::Expect;
    constexpr unsigned int Extent = 16;

    std::shared_ptr<const Assets::TextureData> Texture(const std::uint64_t id,
        const TestSupport::Rgba color = { 255, 255, 255, 255 })
    {
        auto result = std::make_shared<Assets::TextureData>();
        result->id = id;
        result->width = result->height = 1;
        result->pixels = { static_cast<std::byte>(color.r), static_cast<std::byte>(color.g),
            static_cast<std::byte>(color.b), static_cast<std::byte>(color.a) };
        return result;
    }

    std::shared_ptr<Assets::MeshData> SlopingQuad()
    {
        auto result = std::make_shared<Assets::MeshData>();
        result->id = 0x1000'0000'0070'0001ull;
        const auto corner = [](const float x, const float y)
        {
            Core::MeshVertex vertex;
            vertex.position = { x, y, x + 3 };
            vertex.normal = { 0.70710678f, 0, -0.70710678f };
            vertex.textureCoordinate = { 0.5f, 0.5f };
            return vertex;
        };
        result->vertices = { corner(-1, -1), corner(-1, 1), corner(1, 1), corner(1, -1) };
        result->indices = { 0, 1, 2, 0, 2, 3 };
        return result;
    }

    void SetCamera(RenderFrameBuilder& builder)
    {
        builder.SetRenderTargetSize({ Extent, Extent });
        CameraRenderData camera;
        camera.clearColor = Math::Color::Black;
        camera.projection = Math::Matrix4x4::CreateOrthographicLeftHanded(6, 4, 0.1f, 10);
        builder.SetCamera(camera);
    }

    RenderFrame LitFrame(const unsigned int mode)
    {
        RenderFrameBuilder builder;
        SetCamera(builder);
        LightRenderData light;
        light.direction = { 0, 0, 1 };
        light.color = Math::Color::White;
        static_cast<void>(builder.AddLight(light));
        const auto material = builder.AddMaterial({ Texture(0x5000'0000'0070'0001ull) });
        auto mesh = SlopingQuad();
        if (mode == 0 || mode == 3)
        {
            MeshDraw draw;
            draw.pipeline = builder.AddPipeline({ PipelineKind::Mesh });
            draw.geometry = builder.AddGeometry({ mesh });
            draw.material = material;
            draw.localToWorld = mode == 3
                ? Math::Matrix4x4::CreateScale({ 1, 1, 0 }) * Math::Matrix4x4::CreateTranslation({ 0, 0, 3 })
                : Math::Matrix4x4::CreateScale({ 2, 1, 1 });
            static_cast<void>(builder.TryAddDraw(RenderPass::Opaque, draw));
        }
        else
        {
            auto skinned = std::make_shared<Assets::SkinnedMeshData>();
            skinned->id = 0x1000'0000'0070'0002ull;
            skinned->indices = mesh->indices;
            for (const auto& source : mesh->vertices)
            {
                Core::SkinnedMeshVertex vertex;
                vertex.position = source.position;
                vertex.normal = source.normal;
                vertex.textureCoordinate = source.textureCoordinate;
                vertex.boneIndices = { 0, 0, 0, 0 };
                vertex.boneWeights = { 1, 0, 0, 0 };
                skinned->vertices.push_back(vertex);
            }
            skinned->bounds = Assets::ComputeSkinnedBounds(skinned->vertices);
            SkinnedMeshDraw draw;
            draw.pipeline = builder.AddPipeline({ PipelineKind::SkinnedMesh });
            draw.geometry = builder.AddSkinnedGeometry({ skinned });
            draw.material = material;
            draw.localToWorld = mode == 1 ? Math::Matrix4x4::CreateScale({ 2, 1, 1 })
                                        : Math::Matrix4x4::Identity();
            draw.boneMatrices = std::make_shared<std::vector<Math::Matrix4x4>>(1,
                mode == 2 ? Math::Matrix4x4::CreateScale({ 2, 1, 1 }) : Math::Matrix4x4::Identity());
            static_cast<void>(builder.TryAddDraw(RenderPass::Opaque, draw));
        }
        return std::move(builder).Build();
    }

    SpriteDraw ScreenQuad(RenderFrameBuilder& builder, const MaterialHandle material,
        const Math::Color tint = Math::Color::White)
    {
        SpriteDraw draw;
        draw.pipeline = builder.AddPipeline({ PipelineKind::Sprite });
        draw.material = material;
        draw.tint = tint;
        draw.space = DrawSpace::Screen;
        draw.localToWorld = Math::Matrix4x4::CreateScale({ Extent, Extent, 1 }) *
            Math::Matrix4x4::CreateTranslation({ Extent * 0.5f, Extent * 0.5f, 0 });
        return draw;
    }

    RenderFrame MixedBudgetFrame()
    {
        RenderFrameBuilder builder;
        SetCamera(builder);
        builder.AddAmbientLight(Math::Color::White);
        const auto material = builder.AddMaterial({ Texture(0x5000'0000'0070'0001ull) });
        MeshDraw mesh;
        mesh.pipeline = builder.AddPipeline({ PipelineKind::Mesh });
        mesh.geometry = builder.AddGeometry({ SlopingQuad() });
        mesh.material = material;
        mesh.tint = { 0, 0, 1, 1 };
        static_cast<void>(builder.TryAddDraw(RenderPass::Opaque, mesh));
        TilemapDraw tiles;
        tiles.pipeline = builder.AddPipeline({ PipelineKind::Sprite });
        tiles.material = material;
        tiles.tiles = std::make_shared<std::vector<TilemapTile>>(
            FrameSubmissionBudget.maximumQuadsPerFrame - 1);
        tiles.localToWorld = Math::Matrix4x4::CreateTranslation({ 1000, 1000, 2 });
        static_cast<void>(builder.TryAddDraw(RenderPass::Transparent, tiles));
        static_cast<void>(builder.TryAddDraw(RenderPass::Overlay, ScreenQuad(builder, material, { 0, 1, 0, 1 }), 0));
        static_cast<void>(builder.TryAddDraw(RenderPass::Overlay, ScreenQuad(builder, material, { 1, 0, 0, 1 }), 1));
        return std::move(builder).Build();
    }

    RenderFrame ManyMeshesFrame()
    {
        RenderFrameBuilder builder;
        SetCamera(builder);
        builder.AddAmbientLight(Math::Color::White);
        MeshDraw draw;
        draw.pipeline = builder.AddPipeline({ PipelineKind::Mesh });
        draw.geometry = builder.AddGeometry({ SlopingQuad() });
        draw.material = builder.AddMaterial({ Texture(0x5000'0000'0070'0001ull) });
        draw.localToWorld = Math::Matrix4x4::CreateTranslation({ 1000, 0, 0 });
        for (std::size_t index = 0; index < FrameSubmissionBudget.maximumQuadsPerFrame; ++index)
            static_cast<void>(builder.TryAddDraw(RenderPass::Opaque, draw));
        draw.localToWorld = Math::Matrix4x4::Identity();
        draw.tint = { 0, 1, 0, 1 };
        static_cast<void>(builder.TryAddDraw(RenderPass::Opaque, draw));
        return std::move(builder).Build();
    }
}

bool RunRenderingStabilityPixelTests()
{
    const auto supported = TestSupport::SupportedBackends();
    if (supported.empty())
    {
        std::cout << "  rendering stability pixels skipped: no supported graphics backend\n";
        return true;
    }
    bool passed = true;
    for (const auto* backend : supported)
    {
        auto device = backend->CreateDevice();
        if (!Expect(device && device->Initialize({}), "the stability test graphics device initializes")) return false;
        std::cout << "  stability pixels on " << backend->id << '\n';
        CapturedImage image;
        // The transformed plane has tangent (2, 0, 1); its normal points along (1, 0, -2).
        // This expected brightness comes from the surface, independently of the CPU normal matrix.
        const int expected = static_cast<int>(std::lround(255 * LinearChannelToSrgb(2 / std::sqrt(5.0f))));
        for (unsigned int mode = 0; mode < 4; ++mode)
        {
            passed &= Expect(device->RenderToImage(LitFrame(mode), image) && image.IsValid(),
                "nonuniformly scaled static, skinned-world, skinned-bone, and flattened meshes render");
            if (!image.IsValid()) return false;
            const auto pixel = TestSupport::ReadPixel(image, Extent / 2, Extent / 2);
            const int expectedChannel = mode == 3 ? 255 : expected;
            passed &= Expect(std::abs(static_cast<int>(pixel.r) - expectedChannel) <= 2 &&
                std::abs(static_cast<int>(pixel.g) - expectedChannel) <= 2 &&
                std::abs(static_cast<int>(pixel.b) - expectedChannel) <= 2,
                "lighting agrees with the normal of the scaled surface");
        }
        passed &= Expect(device->RenderToImage(MixedBudgetFrame(), image) &&
            TestSupport::ReadPixel(image, Extent / 2, Extent / 2) == TestSupport::Rgba{ 0, 255, 0, 255 },
            "a mesh plus the exact quad limit preserves the final green overlay and rejects overflow red");
        passed &= Expect(device->RenderToImage(ManyMeshesFrame(), image) &&
            TestSupport::ReadPixel(image, Extent / 2, Extent / 2) == TestSupport::Rgba{ 0, 255, 0, 255 },
            "static mesh submission grows beyond the unrelated quad limit");
        for (std::size_t index = 0; index <= TextureCacheBudget.maximumEntries + 3; ++index)
        {
            RenderFrameBuilder builder;
            SetCamera(builder);
            const auto material = builder.AddMaterial({ Texture(0x5000'0000'0071'0000ull + index,
                { 17, 211, 53, 255 }) });
            static_cast<void>(builder.TryAddDraw(RenderPass::Overlay, ScreenQuad(builder, material)));
            const IGraphicsDevice::CaptureRequest request{ static_cast<unsigned int>(index % 4), false };
            if (!Expect(device->RenderToImage(std::move(builder).Build(), image, request) &&
                TestSupport::ReadPixel(image, Extent / 2, Extent / 2) == TestSupport::Rgba{ 17, 211, 53, 255 },
                "completed headless captures retire old assets across alternating channels"))
            {
                passed = false;
                break;
            }
        }
    }
    return passed;
}

static const TestSupport::Registration gRenderingStabilityPixelTests{
    "BackendImage", "rendering stability regressions produce correct pixels on every backend",
    RunRenderingStabilityPixelTests };
