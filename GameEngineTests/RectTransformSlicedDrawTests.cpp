#include "RectTransformSlicedDrawTests.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/AssetReference.h"
#include "BackendPixelSupport.h"
#include "Math/Matrix.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Platform/NativeSurface.h"
#include "Platform/PlatformServices.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Rendering/TextRasterizationCache.h"
#include "Runtime/Canvas.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/RectTransform.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/Transform.h"
#include "Runtime/UILayoutSystem.h"
#include "SceneRendering/SceneRenderPass.h"
#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::MakeRoundedTexture;
using TestSupport::ReadPixel;
using TestSupport::Rgba;
using TestSupport::SupportedBackends;

/// <summary>
/// RectTransform·SpriteRenderer·Canvas를 구성하고 SceneRenderPass가 실제 생성한 Geometry를 검사한다.
/// PNG 디코더의 결과와 독립적으로 9-슬라이스 배치를 확인한다.
/// </summary>
namespace
{
    /// <summary>그림의 크기와 테두리다. 편집기의 button-32.png가 선언하는 값과 같다.</summary>
    constexpr unsigned int TextureExtent = 32;
    constexpr float AuthoredBorder = 8.0f;

    /// <summary>
    /// 단추의 논리 크기다.
    /// </summary>
    constexpr float ButtonLogicalWidth = 50.0f;
    constexpr float ButtonLogicalHeight = 24.0f;

    /// <summary>그리는 면이다. 배율 2의 단추와 여백이 들어간다.</summary>
    constexpr unsigned int ImageWidth = 256;
    constexpr unsigned int ImageHeight = 128;
    constexpr float Margin = 16.0f;

    constexpr Rgba FillColor{ 255, 255, 255, 255 };
    constexpr GameEngine::Math::Color ClearColor{
        64.0f / 255.0f, 96.0f / 255.0f, 32.0f / 255.0f, 1.0f };

    /// <summary>SceneRenderPass가 이 단추에 대해 실제로 실은 값들이다.</summary>
    struct EmittedGeometry
    {
        bool found = false;
        GameEngine::Math::Vector2 size;
        GameEngine::Rendering::SpriteBorder border;
        float borderScale = 0.0f;
        bool screenSpace = false;
        bool sliced = false;
    };

    /// <summary>1x1 RGBA PNG다. 기하만 꺼내므로 픽셀이 무엇인지는 상관없지만, 텍스처를 얻지
    /// 못하면 draw 자체가 실리지 않으므로 진짜 이미지여야 한다.</summary>
    constexpr unsigned char OnePixelPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0x0F, 0x04, 0x00, 0x09, 0xFB, 0x03, 0xFD, 0xFB, 0x5E, 0x6B, 0x2B, 0x00, 0x00, 0x00, 0x00,
        0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

    /// <summary>편집기의 단추와 같은 계층을 세우고, SceneRenderPass가 낸 sliced draw를 꺼낸다.</summary>
    [[nodiscard]] EmittedGeometry CollectButtonGeometry(
        const std::filesystem::path& root, const float scale)
    {
        using namespace GameEngine;

        const Platform::DirectoryContentSource content(root);
        Runtime::Game game{ nullptr, nullptr };
        if (!game.Initialize(content, {}))
        {
            return {};
        }
        game.SetRenderSurfaceSize(
            static_cast<float>(ImageWidth), static_cast<float>(ImageHeight));

        auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "Button");
        Runtime::GameObject* const canvasObject = scene->CreateGameObject("Canvas");
        Runtime::Canvas* const canvas = canvasObject->AddComponent<Runtime::Canvas>();
        canvas->SetScaleFactor(scale);

        Runtime::GameObject* const button = scene->CreateGameObject("Button");
        static_cast<void>(button->GetTransform().SetParent(&canvasObject->GetTransform()));
        Runtime::RectTransform* const rect = button->AddComponent<Runtime::RectTransform>();
        // 왼쪽 위에 붙이고 논리 크기로 편다. 배율이 곱해지는 자리는 배치이지 이 값이 아니다.
        rect->SetAnchorMin({ 0.0f, 0.0f });
        rect->SetAnchorMax({ 0.0f, 0.0f });
        rect->SetOffsetMin({ Margin, Margin });
        rect->SetOffsetMax({ Margin + ButtonLogicalWidth, Margin + ButtonLogicalHeight });

        Runtime::SpriteRenderer* const renderer = button->AddComponent<Runtime::SpriteRenderer>();
        renderer->SetSprite(Assets::AssetReference::Parse("Sprites/button.png"));
        renderer->SetDrawMode(Runtime::SpriteRenderer::DrawMode::Sliced);
        renderer->SetSpace(Runtime::SpriteRenderer::Space::Screen);

        static_cast<void>(game.GetSceneManager().AddScene(std::move(scene)));
        const Runtime::UILayoutSystem layout;
        layout.Synchronize(
            game.GetSceneManager(), static_cast<float>(ImageWidth),
            static_cast<float>(ImageHeight));

        Rendering::RenderFrameBuilder builder;
        SceneRendering::SceneRenderPass frontend{
            std::make_shared<Rendering::TextRasterizationCache>(
                Platform::PlatformServices::CreateTextRasterizer()) };
        frontend.Collect(game, builder);
        const Rendering::RenderFrame frame = std::move(builder).Build();

        EmittedGeometry emitted;
        for (const Rendering::SpriteDraw* const draw :
             frame.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Overlay))
        {
            if (!draw || !draw->sliced)
            {
                continue;
            }
            emitted.found = true;
            emitted.size = draw->size;
            emitted.border = draw->border;
            emitted.borderScale = draw->borderScale;
            emitted.screenSpace = draw->space == Rendering::DrawSpace::Screen;
            emitted.sliced = true;
            break;
        }
        return emitted;
    }

    /// <summary>꺼낸 기하 그대로, 읽을 수 있는 그림으로 한 번 더 그린다.</summary>
    [[nodiscard]] GameEngine::Rendering::RenderFrame BuildFrameFrom(const EmittedGeometry& emitted)
    {
        using namespace GameEngine;

        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ ImageWidth, ImageHeight });
        Rendering::CameraRenderData camera;
        camera.clearColor = ClearColor;
        camera.view = Math::Matrix4x4::Identity();
        camera.projection = Math::Matrix4x4::Identity();
        builder.SetCamera(camera);

        Rendering::SpriteDraw draw;
        draw.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Sprite });
        draw.material = builder.AddMaterial({ MakeRoundedTexture(
            0x5000000000000021ull, TextureExtent, AuthoredBorder, FillColor,
            TestSupport::Quantize(ClearColor)) });
        draw.space = Rendering::DrawSpace::Screen;
        draw.sliced = true;
        draw.pixelsPerUnit = 1.0f;
        // 여기가 요점이다: 크기·테두리·배수를 시험이 정하지 않고 SceneRenderPass에서 받아 쓴다.
        draw.size = emitted.size;
        draw.border = emitted.border;
        draw.borderScale = emitted.borderScale;
        draw.localToWorld = Math::Matrix4x4::CreateTranslation(
            { Margin + emitted.size.GetX() * 0.5f, Margin + emitted.size.GetY() * 0.5f, 0.0f });
        static_cast<void>(builder.TryAddDraw(Rendering::RenderPass::Overlay, draw, 0, 0));

        return std::move(builder).Build();
    }

    [[nodiscard]] bool CheckAtScale(const std::filesystem::path& root, const float scale)
    {
        using namespace GameEngine;

        const EmittedGeometry emitted = CollectButtonGeometry(root, scale);
        if (!Expect(emitted.found, "SceneRenderPass emits a sliced draw for a RectTransform button"))
        {
            return false;
        }

        const auto label = "scale " + std::to_string(static_cast<int>(scale)) + ": ";
        std::cout << "  rect-transform button " << label << "draw size " << emitted.size.GetX()
                  << "x" << emitted.size.GetY() << " px, border " << emitted.border.left
                  << ", borderScale " << emitted.borderScale
                  << (emitted.screenSpace ? ", screen space" : ", world space") << "\n";

        bool passed = true;
        // ⑴ 크기는 해석된 사각형, 곧 물리 픽셀이다. 논리 그대로 실리면 배율 2에서 절반이 된다.
        const float expectedWidth = ButtonLogicalWidth * scale;
        const float expectedHeight = ButtonLogicalHeight * scale;
        if (std::abs(emitted.size.GetX() - expectedWidth) > 0.5f ||
            std::abs(emitted.size.GetY() - expectedHeight) > 0.5f)
        {
            std::cerr << "  " << label << "the draw is " << emitted.size.GetX() << "x"
                      << emitted.size.GetY() << " but the rectangle is " << expectedWidth << "x"
                      << expectedHeight << " physical pixels\n";
        }
        passed &= Expect(
            std::abs(emitted.size.GetX() - expectedWidth) <= 0.5f &&
                std::abs(emitted.size.GetY() - expectedHeight) <= 0.5f,
            "the draw's size is the resolved rectangle in physical pixels");

        // ⑵ 테두리는 그림이 선언한 텍스처 픽셀 그대로, 배수만 캔버스 배율이다.
        passed &= Expect(
            std::abs(emitted.border.left - AuthoredBorder) <= 0.01f,
            "the border stays the texture pixels the sprite declares");
        passed &= Expect(
            std::abs(emitted.borderScale - scale) <= 0.01f,
            "and the canvas scale rides on borderScale instead");
        passed &= Expect(emitted.screenSpace, "and a screen-space button says so");

        // ⑶ 그 기하로 그리면 둥근 부분이 바깥을 향한다 — 손으로 세운 draw가 아니라 이 가지에서.
        for (const Rendering::GraphicsBackendDescriptor* const backend : SupportedBackends())
        {
            const std::unique_ptr<Rendering::IGraphicsDevice> device = backend->CreateDevice();
            if (!device || !device->Initialize(Platform::NativeSurface{}))
            {
                continue;
            }
            Rendering::CapturedImage image;
            if (!Expect(
                    device->RenderToImage(BuildFrameFrom(emitted), image) && image.IsValid(),
                    "the emitted geometry should produce an image"))
            {
                passed = false;
                continue;
            }

            const Rgba background = TestSupport::Quantize(ClearColor);
            const auto left = static_cast<unsigned int>(Margin) + 1;
            const auto right = static_cast<unsigned int>(Margin + emitted.size.GetX()) - 1;
            const auto top = static_cast<unsigned int>(Margin) + 1;
            const auto bottom = static_cast<unsigned int>(Margin + emitted.size.GetY()) - 1;
            const auto widthOfRow = [&](const unsigned int y)
            {
                unsigned int drawn = 0;
                for (unsigned int x = left; x < right; ++x)
                {
                    if (!(ReadPixel(image, x, y) == background))
                    {
                        ++drawn;
                    }
                }
                return drawn;
            };
            const unsigned int topRow = widthOfRow(top);
            const unsigned int middleRow = widthOfRow((top + bottom) / 2);
            const unsigned int bottomRow = widthOfRow(bottom - 1);
            std::cout << "    " << backend->id << " " << label << "rows " << topRow << " / "
                      << middleRow << " / " << bottomRow << "\n";
            if (topRow >= middleRow || bottomRow >= middleRow)
            {
                std::cerr << "  " << backend->id << " " << label << "the button's outermost rows "
                          << "are " << topRow << " and " << bottomRow << " against " << middleRow
                          << " across the middle, so its corners face inwards\n";
            }
            passed &= Expect(
                topRow < middleRow && bottomRow < middleRow,
                "a button laid out by RectTransform has its rounding facing outwards");
        }
        return passed;
    }
}

bool RunRectTransformSlicedDrawTests()
{
    TestSupport::TemporaryDirectory projectDirectory("rect-transform-sliced");
    const std::filesystem::path root = projectDirectory.GetPath();
    const bool wrote =
        TestSupport::WriteFile(root / "Button.gameproject", "{}") &&
        TestSupport::WriteFile(
            root / "Sprites" / "button.png",
            std::string_view(
                reinterpret_cast<const char*>(OnePixelPng), sizeof(OnePixelPng))) &&
        TestSupport::WriteFile(
            root / "Sprites" / "button.png.meta",
            R"({"format": "gameengine-meta/1", "pixelsPerUnit": 16.0,)"
            R"( "border": [8.0, 8.0, 8.0, 8.0]})");
    if (!Expect(wrote, "the rect-transform button test project should be written"))
    {
        return false;
    }

    // 배율 1과 2에서 함께 돈다. 1에서는 논리와 물리가 같은 수라, 사각형이 어느 단위로 실리는지를
    // 묻는 ⑴이 아무것도 지키지 못한다.
    bool passed = CheckAtScale(root, 1.0f);
    passed &= CheckAtScale(root, 2.0f);
    return passed;
}

static const TestSupport::Registration gRectTransformSlicedDrawTests{
    "RenderFrame", "rect transform sliced draw tests should pass",
    RunRectTransformSlicedDrawTests };
