#include "SpriteAtlasReferenceTests.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "Assets/AssetReference.h"
#include "Assets/TextureData.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/Transform.h"
#include "SceneRendering/SceneRenderPass.h"
#include "BackendPixelSupport.h"
#include "TestSupport.h"

namespace
{
// Two-frame sheets: red/green and blue/yellow.
constexpr unsigned char AtlasA[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00,
    0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06,
    0x00, 0x00, 0x00, 0xF4, 0x22, 0x7F, 0x8A, 0x00, 0x00, 0x00, 0x01, 0x73, 0x52, 0x47, 0x42, 0x00,
    0xAE, 0xCE, 0x1C, 0xE9, 0x00, 0x00, 0x00, 0x04, 0x67, 0x41, 0x4D, 0x41, 0x00, 0x00, 0xB1, 0x8F,
    0x0B, 0xFC, 0x61, 0x05, 0x00, 0x00, 0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x0E, 0xC3,
    0x00, 0x00, 0x0E, 0xC3, 0x01, 0xC7, 0x6F, 0xA8, 0x64, 0x00, 0x00, 0x00, 0x0E, 0x49, 0x44, 0x41,
    0x54, 0x18, 0x57, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x04, 0x01, 0x10, 0xF8, 0x03, 0xFD, 0x38,
    0x66, 0x45, 0x59, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
constexpr unsigned char AtlasB[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00,
    0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06,
    0x00, 0x00, 0x00, 0xF4, 0x22, 0x7F, 0x8A, 0x00, 0x00, 0x00, 0x01, 0x73, 0x52, 0x47, 0x42, 0x00,
    0xAE, 0xCE, 0x1C, 0xE9, 0x00, 0x00, 0x00, 0x04, 0x67, 0x41, 0x4D, 0x41, 0x00, 0x00, 0xB1, 0x8F,
    0x0B, 0xFC, 0x61, 0x05, 0x00, 0x00, 0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x0E, 0xC3,
    0x00, 0x00, 0x0E, 0xC3, 0x01, 0xC7, 0x6F, 0xA8, 0x64, 0x00, 0x00, 0x00, 0x11, 0x49, 0x44, 0x41,
    0x54, 0x18, 0x57, 0x63, 0x60, 0x60, 0xF8, 0xFF, 0xFF, 0xFF, 0x7F, 0x86, 0xFF, 0x00, 0x12, 0xF6,
    0x04, 0xFC, 0x75, 0xC6, 0x8E, 0xAE, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42,
    0x60, 0x82 };
}

bool RunSpriteAtlasReferenceTests()
{
    using namespace GameEngine;
    using TestSupport::Expect;
    using TestSupport::Rgba;
    TestSupport::TemporaryDirectory directory("sprite-atlas-reference");
    const auto root = directory.GetPath();
    if (!Expect(
            TestSupport::WriteFile(root / "Test.gameproject", "{}") &&
                TestSupport::WriteFile(root / "a.png",
                    std::string_view(reinterpret_cast<const char*>(AtlasA), sizeof(AtlasA))) &&
                TestSupport::WriteFile(root / "b.png",
                    std::string_view(reinterpret_cast<const char*>(AtlasB), sizeof(AtlasB))) &&
                TestSupport::WriteFile(root / "a.png.meta",
                    R"({"format":"gameengine-meta/1","guid":"00000000000000010000000000000001","pixelsPerUnit":1,"sheet":{"columns":2,"rows":1,"frameCount":2}})") &&
                TestSupport::WriteFile(root / "b.png.meta",
                    R"({"format":"gameengine-meta/1","guid":"00000000000000020000000000000002","pixelsPerUnit":1,"sheet":{"columns":2,"rows":1,"frameCount":2}})"),
            "atlas fixtures should be written"))
    {
        return false;
    }

    const Platform::DirectoryContentSource content(root);
    Runtime::Game game{ nullptr, nullptr };
    if (!Expect(game.Initialize(content, {}), "atlas project should initialize"))
    {
        return false;
    }
    game.SetRenderSurfaceSize(128, 128);
    auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "Atlases");
    const std::array references{ Assets::AssetReference::Parse("00000000000000010000000000000001"),
        Assets::AssetReference::Parse("00000000000000020000000000000002"),
        Assets::AssetReference::Parse("a.png") };
    std::array<Runtime::SpriteRenderer*, 3> renderers{};
    for (std::size_t i = 0; i < renderers.size(); ++i)
    {
        auto* object = scene->CreateGameObject("Sprite" + std::to_string(i));
        object->GetTransform().SetPosition(
            i == 2 ? Math::Vector3{ 0, -2, 2 } : Math::Vector3{ i == 0 ? -2.0f : 2.0f, 0, 2 });
        object->GetTransform().SetScale({ 2, 2, 1 });
        renderers[i] = object->AddComponent<Runtime::SpriteRenderer>();
        renderers[i]->SetSprite(references[i]);
    }
    static_cast<void>(game.GetSceneManager().AddScene(std::move(scene)));
    SceneRendering::SceneRenderPass frontend{ nullptr };
    bool passed = true;
    const auto backends = TestSupport::SupportedBackends();
    passed &= Expect(!backends.empty(), "atlas pixel test requires a graphics backend");
    for (const auto* backend : backends)
    {
        auto device = backend->CreateDevice();
        if (!Expect(device && device->Initialize(Platform::NativeSurface{}),
                "atlas test device should initialize"))
        {
            passed = false;
            continue;
        }
        for (int step = 0; step < 3; ++step)
        {
            renderers[0]->SetSprite(references[step == 2 ? 1 : 0]);
            renderers[1]->SetSprite(references[step == 2 ? 0 : 1]);
            for (auto* renderer : renderers)
            {
                renderer->SetFrame(step == 1 ? 1 : 0);
            }
            Rendering::RenderFrameBuilder builder;
            builder.SetRenderTargetSize({ 128, 128 });
            frontend.Collect(game, builder);
            auto frame = std::move(builder).Build();
            const auto draws =
                frame.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Transparent);
            passed &= Expect(draws.size() == 3, "all atlas references should produce draws");
            Rendering::CapturedImage image;
            if (!Expect(device->RenderToImage(std::move(frame), image) && image.IsValid(),
                    "atlas scene should render"))
            {
                passed = false;
                continue;
            }
            const std::array<Rgba, 3> expected =
                step == 1   ? std::array<Rgba, 3>{ Rgba{ 0, 255, 0, 255 }, Rgba{ 255, 255, 0, 255 },
                      Rgba{ 0, 255, 0, 255 } }
                : step == 2 ? std::array<Rgba, 3>{ Rgba{ 0, 0, 255, 255 }, Rgba{ 255, 0, 0, 255 },
                      Rgba{ 255, 0, 0, 255 } }
                            : std::array<Rgba, 3>{ Rgba{ 255, 0, 0, 255 }, Rgba{ 0, 0, 255, 255 },
                                  Rgba{ 255, 0, 0, 255 } };
            // Sample away from the inter-frame boundary to exclude bilinear atlas bleeding.
            const unsigned int sampleOffset = step == 1 ? 4u : 0u;
            const std::array actual{ TestSupport::ReadPixel(image, 36 + sampleOffset, 64),
                TestSupport::ReadPixel(image, 88 + sampleOffset, 64),
                TestSupport::ReadPixel(image, 62 + sampleOffset, 90) };
            for (std::size_t i = 0; i < actual.size(); ++i)
            {
                const std::string label = std::string(backend->id) + " atlas step " +
                                          std::to_string(step) + " sprite " + std::to_string(i) +
                                          " expected " + TestSupport::Describe(expected[i]) +
                                          " got " + TestSupport::Describe(actual[i]);
                passed &= Expect(actual[i] == expected[i], label.c_str());
            }
        }
        std::cout << "  " << backend->id << ": atlas identity, frames and swaps checked\n";
    }
    return passed;
}

static const TestSupport::Registration gSpriteAtlasReferenceTests{ "BackendImage",
    "sprite atlas references should remain distinct", RunSpriteAtlasReferenceTests };
