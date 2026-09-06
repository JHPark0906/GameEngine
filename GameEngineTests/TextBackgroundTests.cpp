#include "TextBackgroundTests.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>

#include "Assets/TextureData.h"
#include "BackendPixelSupport.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/QuadDrawGeometry.h"
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
#include "Runtime/UILayoutSystem.h"
#include "SceneRendering/SceneRenderPass.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Serialization/SceneSerializer.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using TestSupport::Expect;
    constexpr unsigned int ImageWidth = 512;
    constexpr unsigned int ImageHeight = 256;
    constexpr float PixelsPerUnit = 64.0f;
    constexpr Math::Color ClearColor{ 0.4f, 0.6f, 0.8f, 1.0f };
    constexpr Math::Color BackgroundColor{ 0.0f, 0.0f, 0.0f, 0.6f };
    constexpr float PaddingX = 8.0f;
    constexpr float PaddingY = 4.0f;

    [[nodiscard]] bool Near(const float left, const float right)
    {
        return std::abs(left - right) < 0.001f;
    }

    /// <summary>같은 실제 폰트 배치를 받아 UI와 월드의 배경 기하를 검증하는 장면이다.</summary>
    class TextScene
    {
    public:
        explicit TextScene(const std::shared_ptr<Rendering::TextRasterizationCache>& cache,
            const bool screen, const float scale)
            : mFrontend(cache)
        {
            mGame.SetRenderSurfaceSize(ImageWidth, ImageHeight);
            auto scene = std::make_unique<Runtime::Scene>(mGame.GetRuntimeContext(), "Text background");
            auto* cameraObject = scene->CreateGameObject("Camera");
            auto* camera = cameraObject->AddComponent<Runtime::Camera>();
            camera->SetProjectionMode(Runtime::Camera::ProjectionMode::Orthographic);
            camera->SetOrthographicSize(2.0f);
            camera->SetAspectRatio(2.0f);
            camera->SetClearColor(ClearColor);
            auto* canvasObject = scene->CreateGameObject("Canvas");
            canvasObject->AddComponent<Runtime::Canvas>()->SetScaleFactor(scale);
            mObject = scene->CreateGameObject("Nickname");
            static_cast<void>(mObject->GetTransform().SetParent(&canvasObject->GetTransform()));
            mObject->GetTransform().SetPosition({ 0.0f, 0.0f, 2.0f });
            if (screen)
            {
                mRect = mObject->AddComponent<Runtime::RectTransform>();
                mRect->SetOffsetMin({ 32.0f, 20.0f });
                mRect->SetOffsetMax({ 232.0f, 100.0f });
            }
            mText = mObject->AddComponent<Runtime::TextRenderer>();
            mText->SetSpace(
                screen ? Runtime::TextRenderer::Space::Screen : Runtime::TextRenderer::Space::World);
            mText->SetFontFamily("TextBackgroundFont");
            mText->SetFontSize(20.0f);
            mText->SetText("가\n등반가 Summit");
            mText->SetPixelsPerUnit(PixelsPerUnit);
            mText->SetSortingOrder(20);
            static_cast<void>(mGame.GetSceneManager().AddScene(std::move(scene)));
        }

        [[nodiscard]] Rendering::RenderFrame Collect()
        {
            Runtime::UILayoutSystem{}.Synchronize(mGame.GetSceneManager(), ImageWidth, ImageHeight);
            Rendering::RenderFrameBuilder builder;
            builder.SetRenderTargetSize({ ImageWidth, ImageHeight });
            mFrontend.Collect(mGame, builder);
            return std::move(builder).Build();
        }

        Runtime::Game mGame{ nullptr, nullptr };
        SceneRendering::SceneRenderPass mFrontend;
        Runtime::GameObject* mObject = nullptr;
        Runtime::TextRenderer* mText = nullptr;
        Runtime::RectTransform* mRect = nullptr;
    };

    [[nodiscard]] std::shared_ptr<const Rendering::ShapedText> Shape(
        Rendering::TextRasterizationCache& cache, const Runtime::TextRenderer& text, const float scale)
    {
        Platform::TextRasterizationRequest request;
        request.text = text.GetText();
        request.fontFamily = text.GetFontFamily();
        request.fontSize = text.GetFontSize() * scale;
        request.lineSpacing = text.GetLineSpacing();
        request.alignment = static_cast<Platform::TextAlignment>(text.GetAlignment());
        return cache.Resolve(request);
    }

    [[nodiscard]] bool CheckGeometry(const Rendering::RenderFrame& frame,
        const Rendering::RenderPass pass, const Rendering::ShapedText& shaped, const float paddingScale,
        const Runtime::TextRenderer& renderer)
    {
        const auto& packets = frame.GetDrawPackets(pass);
        if (!Expect(frame.Validate().IsValid(),
                "background frames must satisfy the shared render contract") ||
            !Expect(packets.size() == shaped.runs.size() + 1,
                "a label emits exactly one solid background followed by every glyph page") ||
            !Expect(std::holds_alternative<Rendering::SpriteDraw>(packets.front().payload),
                "stable packet sorting must keep the background before its glyphs"))
        {
            return false;
        }
        const auto& background = std::get<Rendering::SpriteDraw>(packets.front().payload);
        const auto* material = frame.GetMaterial(background.material);
        bool passed =
            Expect(material && material->baseColorTexture && material->baseColorTexture->width == 1 &&
                       material->baseColorTexture->height == 1,
                "the background reuses a one-pixel solid texture");
        const float unit = pass == Rendering::RenderPass::Transparent ? 1.0f / PixelsPerUnit : 1.0f;
        const auto left = background.localToWorld.TransformPoint({ -0.5f * unit, 0.0f, 0.0f });
        const auto right = background.localToWorld.TransformPoint({ 0.5f * unit, 0.0f, 0.0f });
        const auto top = background.localToWorld.TransformPoint({ 0.0f, -0.5f * unit, 0.0f });
        const auto bottom = background.localToWorld.TransformPoint({ 0.0f, 0.5f * unit, 0.0f });
        passed &=
            Expect(Near(right.GetX() - left.GetX(),
                       (static_cast<float>(shaped.width) + 2.0f * PaddingX * paddingScale) * unit) &&
                       Near(bottom.GetY() - top.GetY(),
                           (static_cast<float>(shaped.height) + 2.0f * PaddingY * paddingScale) * unit),
                "background extent is the measured block plus per-side padding, converted to its space "
                "once");
        for (std::size_t index = 1; index < packets.size(); ++index)
        {
            const auto* glyphs = std::get_if<Rendering::TextDraw>(&packets[index].payload);
            passed &= Expect(glyphs && packets[index].sortingOrder == renderer.GetSortingOrder() &&
                                 packets[index].instanceId == packets.front().instanceId &&
                                 packets[index].viewDepth == packets.front().viewDepth,
                "background and glyphs retain one label's sorting identity and depth");
            if (glyphs)
            {
                passed &= Expect(
                    glyphs->localToWorld.GetTranslation() == background.localToWorld.GetTranslation(),
                    "padding expands around the same text center without moving the glyphs");
            }
        }
        return passed;
    }

    [[nodiscard]] bool CheckSceneRoundTrip()
    {
        static_cast<void>(Serialization::RegisterRuntimeComponentFactories());
        Runtime::Game game{ nullptr, nullptr };
        Runtime::Scene scene(game.GetRuntimeContext(), "Text background serialization");
        auto* text = scene.CreateGameObject("Nickname")->AddComponent<Runtime::TextRenderer>();
        text->SetText("등반가");
        text->SetBackgroundColor(BackgroundColor);
        text->SetBackgroundPadding({ PaddingX, PaddingY });
        const auto saved = Serialization::SceneSerializer::SaveToText(scene);
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(saved.data()), saved.size());
        const auto loaded = Serialization::SceneSerializer::LoadFromBytes(
            bytes, "TextBackground.scene", game.GetRuntimeContext());
        const auto* object = loaded ? loaded->FindGameObject("Nickname") : nullptr;
        const auto* restored = object ? object->GetComponent<Runtime::TextRenderer>() : nullptr;
        return Expect(restored && restored->GetText() == text->GetText() &&
                          restored->GetBackgroundColor() == text->GetBackgroundColor() &&
                          restored->GetBackgroundPadding() == text->GetBackgroundPadding(),
            "a saved scene restores nickname text, translucent color, and per-side padding");
    }

    /// <summary>캡처 보존은 요청한 실행에서만 한다. CTest의 보통 실행은 파일을 남기지 않는다.</summary>
    [[nodiscard]] std::string CaptureDirectory()
    {
#ifdef _MSC_VER
        char* value = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&value, &length, "GAMEENGINE_TEXT_BACKGROUND_CAPTURES") != 0 || !value)
        {
            return {};
        }
        std::string result(value);
        std::free(value);
        return result;
#else
        const char* value = std::getenv("GAMEENGINE_TEXT_BACKGROUND_CAPTURES");
        return value ? value : "";
#endif
    }

    void SaveCapture(const Rendering::CapturedImage& image, const std::filesystem::path& directory,
        const std::string& name)
    {
        if (directory.empty())
        {
            return;
        }
        std::string ppm =
            "P6\n" + std::to_string(image.width) + " " + std::to_string(image.height) + "\n255\n";
        for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4)
        {
            ppm.append(reinterpret_cast<const char*>(image.pixels.data() + offset), 3);
        }
        const auto path = directory / (name + ".ppm");
        if (TestSupport::WriteFile(path, ppm))
        {
            std::cout << "  text background capture: " << path.string() << '\n';
        }
    }

    [[nodiscard]] bool CheckPixels(const Rendering::RenderFrame& frame,
        const Rendering::RenderPass pass, Rendering::IGraphicsDevice& device,
        const std::filesystem::path& captures, const std::string& name)
    {
        const auto draws = frame.GetDraws<Rendering::SpriteDraw>(pass);
        if (!Expect(draws.size() == 1, "the pixel fixture has one background"))
        {
            return false;
        }
        const auto quad =
            Rendering::TryBuildSpriteQuad(frame, *draws.front(), { 1, 1 }, "TextBackgroundTest");
        Rendering::CapturedImage image;
        if (!Expect(quad.has_value() && device.RenderToImage(frame, image) && image.IsValid(),
                "each registered graphics backend must render the composed label"))
        {
            return false;
        }
        const auto first = quad->worldViewProjection.TransformPoint({ -0.5f, -0.5f, 0.0f });
        const auto second = quad->worldViewProjection.TransformPoint({ 0.5f, 0.5f, 0.0f });
        const auto left = static_cast<unsigned int>(
            (std::min(first.GetX(), second.GetX()) + 1.0f) * ImageWidth * 0.5f);
        const auto right = static_cast<unsigned int>(
            (std::max(first.GetX(), second.GetX()) + 1.0f) * ImageWidth * 0.5f);
        const auto top = static_cast<unsigned int>(
            (1.0f - std::max(first.GetY(), second.GetY())) * ImageHeight * 0.5f);
        const auto bottom = static_cast<unsigned int>(
            (1.0f - std::min(first.GetY(), second.GetY())) * ImageHeight * 0.5f);
        if (!Expect(right < ImageWidth && bottom < ImageHeight && left > 2 && top > 2,
                "the label and its padding are wholly inside the pixel fixture"))
        {
            return false;
        }
        const auto background = TestSupport::ReadPixel(image, left + 2, top + 1);
        const auto clear = TestSupport::Quantize(ClearColor);
        bool passed =
            Expect(background.r > 10 && background.r < clear.r && background.g > background.r &&
                       background.g < clear.g && background.b > background.g && background.b < clear.b,
                "black alpha 0.6 leaves the colored scene visible through the padding");
        passed &= Expect(TestSupport::ReadPixel(image, left - 2, top - 2) == clear,
            "pixels outside the measured padded block keep the scene color");
        std::size_t brightGlyphPixels = 0;
        for (unsigned int y = top; y < bottom; ++y)
            for (unsigned int x = left; x < right; ++x)
            {
                const auto pixel = TestSupport::ReadPixel(image, x, y);
                brightGlyphPixels += pixel.r > 220 && pixel.g > 220 && pixel.b > 220 ? 1u : 0u;
            }
        passed &= Expect(brightGlyphPixels > 20,
            "white Korean glyphs remain above the dark background in actual GPU output");
        SaveCapture(image, captures, name);
        return passed;
    }
}

bool RunTextBackgroundTests()
{
    using namespace GameEngine;
    auto rasterizer = TestSupport::CreateTestTextRasterizer("TextBackgroundFont");
    if (!TestSupport::Expect(
            rasterizer != nullptr, "the bundled Korean test font must be available"))
    {
        return false;
    }
    auto cache = std::make_shared<Rendering::TextRasterizationCache>(std::move(rasterizer));
    const auto backends = TestSupport::SupportedBackends();
    bool passed = CheckSceneRoundTrip();
    passed &= TestSupport::Expect(
        !backends.empty(), "text background pixel checks require a graphics backend");
    TestSupport::TemporaryDirectory identity("text-background-captures");
    const std::string captureDirectory = CaptureDirectory();
    const auto captures = captureDirectory.empty() ? std::filesystem::path{}
                                                   : std::filesystem::path(captureDirectory) /
                                                         identity.GetPath().filename();
    passed &= TestSupport::ForEachUiScale(
        [&](const float scale)
        {
            bool scalePassed = true;
            TextScene screen(cache, true, scale);
            auto defaultFrame = screen.Collect();
            scalePassed &= Expect(
                defaultFrame.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Overlay)
                        .empty() &&
                    !defaultFrame.GetDraws<Rendering::TextDraw>(Rendering::RenderPass::Overlay)
                        .empty(),
                "existing text remains background-free by default");
            screen.mText->SetBackgroundColor(BackgroundColor);
            screen.mText->SetBackgroundPadding({ PaddingX, PaddingY });
            constexpr std::array alignments{ Runtime::TextRenderer::Alignment::Left,
                Runtime::TextRenderer::Alignment::Center, Runtime::TextRenderer::Alignment::Right };
            constexpr std::array verticalAlignments{ Runtime::TextRenderer::VerticalAlignment::Top,
                Runtime::TextRenderer::VerticalAlignment::Middle,
                Runtime::TextRenderer::VerticalAlignment::Bottom };
            for (const auto alignment : alignments)
                for (const auto verticalAlignment : verticalAlignments)
                {
                    screen.mText->SetAlignment(alignment);
                    screen.mText->SetVerticalAlignment(verticalAlignment);
                    const auto shaped = Shape(*cache, *screen.mText, scale);
                    const auto frame = screen.Collect();
                    if (!Expect(shaped != nullptr && !shaped->runs.empty(),
                            "the multiline fixture must shape"))
                    {
                        return false;
                    }
                    scalePassed &= CheckGeometry(
                        frame, Rendering::RenderPass::Overlay, *shaped, scale, *screen.mText);
                    const auto draws =
                        frame.GetDraws<Rendering::TextDraw>(Rendering::RenderPass::Overlay);
                    if (!draws.empty())
                    {
                        const auto& rect = screen.mRect->GetResolvedRect();
                        const float verticalFactor =
                            verticalAlignment == Runtime::TextRenderer::VerticalAlignment::Top
                                ? 0.0f
                            : verticalAlignment == Runtime::TextRenderer::VerticalAlignment::Middle
                                ? 0.5f
                                : 1.0f;
                        const float horizontalFactor = alignment == Runtime::TextRenderer::Alignment::Left
                            ? 0.0f : alignment == Runtime::TextRenderer::Alignment::Center ? 0.5f : 1.0f;
                        scalePassed &= Expect(
                            Near(draws.front()->localToWorld.GetTranslation().GetX(),
                                rect.x + shaped->width * 0.5f +
                                    (rect.width - shaped->width) * horizontalFactor) &&
                                Near(draws.front()->localToWorld.GetTranslation().GetY(),
                                    rect.y + shaped->height * 0.5f +
                                        (rect.height - shaped->height) * verticalFactor),
                            "text and its background must align within both axes of the RectTransform");
                    }
                }
            TextScene world(cache, false, scale);
            world.mText->SetBackgroundColor(BackgroundColor);
            world.mText->SetBackgroundPadding({ PaddingX, PaddingY });
            const auto shapedWorld = Shape(*cache, *world.mText, 1.0f);
            const auto worldFrame = world.Collect();
            if (!Expect(shapedWorld != nullptr, "world text must shape at its logical size"))
            {
                return false;
            }
            scalePassed &= CheckGeometry(
                worldFrame, Rendering::RenderPass::Transparent, *shapedWorld, 1.0f, *world.mText);
            screen.mText->SetAlignment(Runtime::TextRenderer::Alignment::Center);
            screen.mText->SetVerticalAlignment(Runtime::TextRenderer::VerticalAlignment::Middle);
            const auto screenFrame = screen.Collect();
            for (const auto* backend : backends)
            {
                auto device = backend->CreateDevice();
                if (!Expect(device && device->Initialize(Platform::NativeSurface{}),
                        "text background device must initialize"))
                {
                    scalePassed = false;
                    continue;
                }
                const std::string name =
                    std::string(backend->id) + "-scale" + std::to_string(static_cast<int>(scale));
                scalePassed &= CheckPixels(worldFrame, Rendering::RenderPass::Transparent, *device,
                    captures, name + "-world");
                scalePassed &= CheckPixels(screenFrame, Rendering::RenderPass::Overlay, *device,
                    captures, name + "-screen");
                std::cout << "  " << name
                          << ": measured text backgrounds and Korean glyph order checked\n";
            }
            for (const std::string text : { std::string{}, std::string("   ") })
            {
                screen.mText->SetText(text);
                scalePassed &=
                    Expect(screen.Collect().GetDrawPackets(Rendering::RenderPass::Overlay).empty(),
                        "empty and ink-free text must not leave an orphan background");
            }
            screen.mText->SetText("Hidden");
            screen.mRect->SetOffsetMin({ 800.0f, 800.0f });
            screen.mRect->SetOffsetMax({ 900.0f, 900.0f });
            scalePassed &=
                Expect(screen.Collect().GetDrawPackets(Rendering::RenderPass::Overlay).empty(),
                    "a fully clipped screen label hides its background together with its glyphs");
            world.mObject->GetTransform().SetPosition(
                { 4.0f + shapedWorld->width * 0.5f / PixelsPerUnit + 0.2f, 0.0f, 2.0f });
            world.mText->SetBackgroundPadding({ 64.0f, PaddingY });
            scalePassed &=
                Expect(!world.Collect()
                           .GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Transparent)
                           .empty(),
                    "world culling must retain padding that reaches the camera when the text block "
                    "is outside");
            world.mText->SetBackgroundColor({ 0, 0, 0, 0 });
            scalePassed &=
                Expect(world.Collect().GetDrawPackets(Rendering::RenderPass::Transparent).empty(),
                    "disabled background padding must not keep an offscreen text block visible");
            return scalePassed;
        });
    return passed;
}

static const TestSupport::Registration gTextBackgroundTests{ "BackendImage",
    "text backgrounds should follow measured text and precede glyphs", RunTextBackgroundTests };
