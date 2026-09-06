#include "SpritePingPongTests.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>

#include "Assets/AssetReference.h"
#include "Core/Json.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/SpriteAnimator.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/Transform.h"
#include "SceneRendering/SceneRenderPass.h"
#include "Serialization/SceneSerializer.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "BackendPixelSupport.h"
#include "TestSupport.h"

bool RunSpritePingPongTests()
{
    using namespace GameEngine;
    using namespace Runtime;
    using TestSupport::Expect;
    bool passed = true;
    constexpr std::array expected{ 0, 1, 2, 1, 0, 1, 2, 1, 0 };
    for (int step = 0; step < static_cast<int>(expected.size()); ++step)
    {
        passed &= Expect(SelectAnimationFrame(static_cast<float>(step), 5, 3, 1, true, true)
                == 5 + expected[step], "ping-pong must reflect without duplicating endpoints");
        passed &= Expect(SelectAnimationFrame(static_cast<float>(step), 0, 3, 1, true)
                == step % 3, "default forward loop must remain compatible");
    }
    passed &= Expect(SelectAnimationFrame(100, 0, 3, 1, false, true) == 0 &&
            SelectAnimationFrame(3, 0, 3, 1, false, true) == 1,
        "non-looping ping-pong must return once and hold the first frame");
    passed &= Expect(SelectAnimationFrame(100, 4, 1, 1, true, true) == 4 &&
            SelectAnimationFrame(3, 0, 2, 1, true, true) == 1,
        "one- and two-frame clips must be well-defined");
    passed &= Expect(SelectAnimationFrame(3.9f, 0, 3, 1, true, true) == 1 &&
            SelectAnimationFrame(1000003, 0, 3, 1, true, true) == 1,
        "fractional times and dropped frames must use the correct returning frame");
    passed &= Expect(SelectAnimationFrame(-1, 5, 3, 1, true, true) == 5 &&
            SelectAnimationFrame(4, 5, 3, 0, true, true) == 5 &&
            SelectAnimationFrame(std::numeric_limits<float>::infinity(), 5, 3, 1, true, true) == 5,
        "invalid time and stopped rate must hold the first frame");

    ObjectRegistry registry;
    const Input input;
    RuntimeContext context(registry, input);
    GameObject object(context, "Animated");
    auto* renderer = object.AddComponent<SpriteRenderer>();
    auto* animator = object.AddComponent<SpriteAnimator>();
    animator->SetFrameRate(1);
    animator->SetPingPong(true);
    object.Update(3);
    passed &= Expect(renderer->ResolveFrame(3) == 1 && renderer->ResolveFrame(4) == 3,
        "whole-sheet playback must resolve against each current asset's frame count");
    animator->SetPlaying(false);
    object.Update(10);
    passed &= Expect(renderer->ResolveFrame(3) == 1, "pause must keep the returning frame");
    animator->SetEnabled(false);
    object.Update(10);
    passed &= Expect(renderer->ResolveFrame(3) == 1, "disabled animator must preserve its sample");
    renderer->SetFrame(2);
    passed &= Expect(renderer->ResolveFrame(3) == 2, "manual frame must override deferred animation");
    animator->SetEnabled(true);
    animator->Restart();
    object.Update(0);
    passed &= Expect(renderer->ResolveFrame(3) == 0, "restart must reset paused animation");
    animator->SetPlaying(true);
    animator->SetFrameCount(3);
    object.Update(3);
    passed &= Expect(renderer->GetFrame() == 1, "explicit clips must drive the renderer directly");
    animator->SetPingPong(false);
    object.Update(0);
    passed &= Expect(renderer->ResolveFrame(3) == 0, "switching to forward must discard deferred playback");

    GameObject restored(context, "Restored");
    static_cast<void>(Serialization::RegisterComponentType(SpriteAnimator::StaticType()));
    animator->SetPingPong(true);
    const auto snapshot = Serialization::SceneSerializer::SaveComponentToJson(*animator);
    auto* component = snapshot ? Serialization::SceneSerializer::LoadComponentIntoGameObject(*snapshot, restored) : nullptr;
    auto* loaded = restored.GetComponent<SpriteAnimator>();
    passed &= Expect(component && loaded && loaded->IsPingPong(), "ping-pong must survive scene serialization");
    GameObject legacy(context, "Legacy");
    static_cast<void>(Serialization::SceneSerializer::LoadComponentIntoGameObject(
        Core::Json::Parse(R"({"type":"SpriteAnimator","loop":true})"), legacy));
    const auto* old = legacy.GetComponent<SpriteAnimator>();
    passed &= Expect(old && !old->IsPingPong(), "old scenes must default to forward playback");
    return passed;
}

bool RunSpritePingPongImageTests()
{
    using namespace GameEngine;
    using TestSupport::Expect;
    // Three pixels: red, green, blue. The returning frame must be green, not red.
    constexpr unsigned char png[] = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0,0,0,0x0D,0x49,0x48,0x44,0x52,
        0,0,0,3,0,0,0,1,8,6,0,0,0,0x1B,0xE0,0x14,0xB4,0,0,0,1,0x73,0x52,0x47,0x42,
        0,0xAE,0xCE,0x1C,0xE9,0,0,0,4,0x67,0x41,0x4D,0x41,0,0,0xB1,0x8F,0x0B,0xFC,
        0x61,5,0,0,0,9,0x70,0x48,0x59,0x73,0,0,0x0E,0xC3,0,0,0x0E,0xC3,1,0xC7,0x6F,
        0xA8,0x64,0,0,0,0x12,0x49,0x44,0x41,0x54,0x18,0x57,0x63,0xF8,0xCF,0xC0,0xF0,
        0x1F,0x0C,0x19,0xFE,0xFF,7,0,0x23,0xE9,5,0xFB,0xC5,0x1E,0xEF,0x34,0,0,0,0,
        0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82 };
    TestSupport::TemporaryDirectory directory("sprite-ping-pong");
    const auto root = directory.GetPath();
    if (!Expect(TestSupport::WriteFile(root / "Test.gameproject", "{}") &&
            TestSupport::WriteFile(root / "sheet.png",
                std::string_view(reinterpret_cast<const char*>(png), sizeof(png))) &&
            TestSupport::WriteFile(root / "sheet.png.meta",
                R"({"format":"gameengine-meta/1","guid":"00000000000000030000000000000003","pixelsPerUnit":1,"sheet":{"columns":3,"rows":1,"frameCount":3}})"),
            "three-frame fixture must be written")) return false;
    const Platform::DirectoryContentSource content(root);
    Runtime::Game game{ nullptr, nullptr };
    if (!Expect(game.Initialize(content, {}), "ping-pong project must initialize")) return false;
    game.SetRenderSurfaceSize(128, 128);
    auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "PingPong");
    auto* object = scene->CreateGameObject("Sprite");
    object->GetTransform().SetPosition({ 0, 0, 2 });
    object->GetTransform().SetScale({ 3, 3, 1 });
    auto* renderer = object->AddComponent<Runtime::SpriteRenderer>();
    renderer->SetSprite(Assets::AssetReference::Parse("00000000000000030000000000000003"));
    auto* animator = object->AddComponent<Runtime::SpriteAnimator>();
    animator->SetPingPong(true);
    animator->SetFrameRate(1);
    static_cast<void>(game.GetSceneManager().AddScene(std::move(scene)));
    SceneRendering::SceneRenderPass frontend{ nullptr };
    bool passed = true;
    const auto backends = TestSupport::SupportedBackends();
    passed &= Expect(!backends.empty(), "ping-pong pixel test requires a backend");
    for (const auto* backend : backends)
    {
        auto device = backend->CreateDevice();
        if (!Expect(device && device->Initialize(Platform::NativeSurface{}), "device must initialize"))
        {
            passed = false;
            continue;
        }
        animator->Restart();
        for (int step = 0; step < 9; ++step)
        {
            object->Update(step == 0 ? 0.0f : 1.0f);
            Rendering::RenderFrameBuilder builder;
            builder.SetRenderTargetSize({ 128, 128 });
            frontend.Collect(game, builder);
            auto frame = std::move(builder).Build();
            const auto draws = frame.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Transparent);
            constexpr std::array indices{ 0, 1, 2, 1, 0, 1, 2, 1, 0 };
            passed &= Expect(draws.size() == 1 &&
                std::abs(draws[0]->uvRect.u - static_cast<float>(indices[step]) / 3) < 0.001f,
                "scene collection must resolve the returning sheet frame");
            Rendering::CapturedImage image;
            passed &= Expect(device->RenderToImage(std::move(frame), image) && image.IsValid(),
                "ping-pong frame must render");
            if (image.IsValid())
            {
                const auto pixel = TestSupport::ReadPixel(image, 64, 64);
                const int dominant = indices[step] == 0 ? pixel.r : indices[step] == 1 ? pixel.g : pixel.b;
                const int others = pixel.r + pixel.g + pixel.b - dominant;
                // One-pixel cells pick up a small neighbouring contribution with linear sampling.
                passed &= Expect(dominant > 230 && others < 40, "GPU must display the expected animation color");
            }
        }
        std::cout << "  " << backend->id << ": 0 1 2 1 0 1 2 1 0 verified\n";
    }
    return passed;
}

static const TestSupport::Registration gSpritePingPongTests{
    "RuntimeObject", "sprite ping-pong playback should pass", RunSpritePingPongTests };
static const TestSupport::Registration gSpritePingPongImageTests{
    "BackendImage", "sprite ping-pong pixels should pass", RunSpritePingPongImageTests };
