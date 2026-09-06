#include "TextMaskTests.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

#include "BackendPixelSupport.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Rendering/TextRasterizationCache.h"
#include "Runtime/Camera.h"
#include "Runtime/Canvas.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/RectMask.h"
#include "Runtime/RectTransform.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "SceneRendering/SceneRenderPass.h"
#include "TestSupport.h"

bool RunTextMaskTests()
{
    using namespace GameEngine;
    const auto backends = TestSupport::SupportedBackends();
    if (!TestSupport::Expect(!backends.empty(), "text masks require a graphics backend")) return false;
    auto cache = std::make_shared<Rendering::TextRasterizationCache>(
        TestSupport::CreateTestTextRasterizer("Mask font"));
    bool passed = true;
    for (const unsigned int scale : { 1u, 2u })
    {
        Runtime::Game game(nullptr, nullptr);
        auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "Nested text masks");
        scene->CreateGameObject("Camera")->AddComponent<Runtime::Camera>()->SetClearColor({ 0, 0, 0, 1 });
        auto* canvas = scene->CreateGameObject("Canvas");
        canvas->AddComponent<Runtime::Canvas>()->SetScaleFactor(static_cast<float>(scale));
        const auto rectangle = [&](const char* name, Runtime::GameObject* parent,
            const Math::Vector2& first, const Math::Vector2& last)
        {
            auto* object = scene->CreateGameObject(name);
            static_cast<void>(object->GetTransform().SetParent(&parent->GetTransform()));
            auto* rect = object->AddComponent<Runtime::RectTransform>();
            rect->SetOffsetMin(first);
            rect->SetOffsetMax(last);
            return object;
        };
        auto* outer = rectangle("Outer", canvas, { 24, 20 }, { 108, 112 });
        auto* outerMask = outer->AddComponent<Runtime::RectMask>();
        auto* inner = rectangle("Inner", outer, { -8, 16 }, { 72, 58 });
        auto* innerMask = inner->AddComponent<Runtime::RectMask>();
        auto* label = rectangle("Label", inner, { -12, -12 }, { 200, 100 });
        auto* text = label->AddComponent<Runtime::TextRenderer>();
        text->SetFontFamily("Mask font");
        text->SetFontSize(32);
        text->SetText("가나다라 ABCDE\n마바사아 FGHIJ");
        text->SetBackgroundColor({ 1, 0, 0, .3f });
        text->SetBackgroundPadding({ 4, 4 });
        static_cast<void>(game.GetSceneManager().AddScene(std::move(scene)));
        game.SetRenderSurfaceSize(128.0f * scale, 128.0f * scale);
        game.Update(0);
        const auto clip = inner->GetComponent<Runtime::RectTransform>()->GetVisibleRect();
        SceneRendering::SceneRenderPass frontend(cache);
        const auto collect = [&]()
        {
            Rendering::RenderFrameBuilder builder;
            builder.SetRenderTargetSize({ 128 * scale, 128 * scale });
            frontend.Collect(game, builder);
            return std::move(builder).Build();
        };
        const auto masked = collect();
        // The unmasked frame reuses the same shaping cache. Any mutation of cached glyphs would
        // also clip this reference, and any wrong UV adjustment would change its interior pixels.
        static_cast<void>(inner->RemoveComponent(innerMask));
        static_cast<void>(outer->RemoveComponent(outerMask));
        game.Update(0);
        const auto original = collect();
        for (const auto* backend : backends)
        {
            auto device = backend->CreateDevice();
            Rendering::CapturedImage image;
            Rendering::CapturedImage reference;
            if (!TestSupport::Expect(device && device->Initialize(Platform::NativeSurface{}) &&
                device->RenderToImage(masked, image) && device->RenderToImage(original, reference),
                "nested text masks must render on each registered backend")) { passed = false; continue; }
            std::size_t insideInk = 0;
            std::size_t removedPixels = 0;
            bool outsideClear = true;
            bool interiorUnchanged = true;
            for (unsigned int y = 0; y < image.height; ++y)
                for (unsigned int x = 0; x < image.width; ++x)
                {
                    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
                    const bool inside = clip.Contains(static_cast<float>(x) + .5f, static_cast<float>(y) + .5f);
                    for (std::size_t channel = 0; channel < 3; ++channel)
                    {
                        const int actual = std::to_integer<int>(image.pixels[offset + channel]);
                        const int unmasked = std::to_integer<int>(reference.pixels[offset + channel]);
                        if (inside) interiorUnchanged &= std::abs(actual - unmasked) <= 2;
                        else
                        {
                            outsideClear &= actual == 0;
                            if (unmasked > 0) ++removedPixels;
                        }
                    }
                    if (inside && std::to_integer<int>(image.pixels[offset + 1]) > 100) ++insideInk;
                }
            passed &= TestSupport::Expect(outsideClear && removedPixels > 100,
                "partial text and its background must leave every pixel outside nested masks untouched");
            passed &= TestSupport::Expect(interiorUnchanged && insideInk > 20,
                "clipping must preserve visible glyph pixels and atlas UVs without squeezing or corrupting the cache");
        }
    }
    return passed;
}

static const TestSupport::Registration gTextMaskTests{
    "BackendImage", "nested text masks must clip geometry and atlas UVs", RunTextMaskTests };
