#include "GameViewTextClipTests.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Platform/PlatformServices.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Rendering/TextRasterizationCache.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "SceneRendering/SceneRenderPass.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    namespace Rendering = GameEngine::Rendering;
    namespace Runtime = GameEngine::Runtime;

    /// <summary>화면 공간에서 글자 블록이 실제로 덮는 사각형이다.</summary>
    struct TextBounds
    {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        bool found = false;
    };

    /// <summary>
    /// 오버레이 패스의 글자 draw들이 화면에서 덮는 사각형을 잰다.
    ///
    /// 글리프 quad의 중심은 <b>블록 중심</b>을 원점으로 한 값이므로, 화면 자리는 그 draw의
    /// 이동을 더해야 나온다. 재는 것이 이 합인 이유는 그것이 백엔드가 실제로 그리는 자리이기
    /// 때문이다 — 이동만 보거나 quad만 보면 둘 중 하나가 틀려도 알 수 없다.
    /// </summary>
    [[nodiscard]] TextBounds MeasureOverlayText(const Rendering::RenderFrame& frame)
    {
        TextBounds bounds;
        bounds.left = std::numeric_limits<float>::max();
        bounds.top = std::numeric_limits<float>::max();
        bounds.right = std::numeric_limits<float>::lowest();
        bounds.bottom = std::numeric_limits<float>::lowest();
        for (const Rendering::TextDraw* const draw :
            frame.GetDraws<Rendering::TextDraw>(Rendering::RenderPass::Overlay))
        {
            if (!draw || !draw->glyphs)
            {
                continue;
            }
            const GameEngine::Math::Vector3 translation = draw->localToWorld.GetTranslation();
            for (const Rendering::TextGlyphQuad& glyph : *draw->glyphs)
            {
                bounds.found = true;
                bounds.left =
                    std::min(bounds.left, translation.GetX() + glyph.centerX - glyph.width * 0.5f);
                bounds.right =
                    std::max(bounds.right, translation.GetX() + glyph.centerX + glyph.width * 0.5f);
                bounds.top =
                    std::min(bounds.top, translation.GetY() + glyph.centerY - glyph.height * 0.5f);
                bounds.bottom = std::max(
                    bounds.bottom, translation.GetY() + glyph.centerY + glyph.height * 0.5f);
            }
        }
        return bounds;
    }
}

bool RunGameViewTextClipTests()
{
    namespace Assets = GameEngine::Assets;
    namespace Math = GameEngine::Math;
    namespace Platform = GameEngine::Platform;
    namespace SceneRendering = GameEngine::SceneRendering;

    constexpr float ViewWidth = 800.0f;
    constexpr float ViewHeight = 600.0f;
    constexpr float TextX = 24.0f;
    constexpr float TextY = 24.0f;

    Runtime::Game game{ nullptr, nullptr };
    game.SetRenderSurfaceSize(ViewWidth, ViewHeight);

    // 샘플 게임의 RuntimeText와 같은 모양이다: 화면 공간 글자이고, RectTransform 없이 Transform
    // 하나만 갖는다. 게임이 UI 사각형을 세우지 않고 화면 좌표를 직접 주는 흔한 방식이다.
    auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "Text");
    Runtime::GameObject* const object = scene->CreateGameObject("RuntimeText");
    object->GetTransform().SetPosition({ TextX, TextY, 0.0f });
    Runtime::TextRenderer* const text = object->AddComponent<Runtime::TextRenderer>();
    text->SetText("Game Engine Text");
    text->SetFontFamily("Segoe UI");
    text->SetFontSize(32.0f);
    text->SetSpace(Runtime::TextRenderer::Space::Screen);
    static_cast<void>(game.GetSceneManager().AddScene(std::move(scene)));

    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer)
    {
        std::cout << "  game view text clip tests skipped: no bundled font on this machine\n";
        return true;
    }
    Rendering::RenderFrameBuilder builder;
    SceneRendering::SceneRenderPass frontend{
        std::make_shared<Rendering::TextRasterizationCache>(std::move(rasterizer)) };
    frontend.Collect(game, builder);
    const Rendering::RenderFrame frame = std::move(builder).Build();

    const TextBounds bounds = MeasureOverlayText(frame);
    if (!Expect(bounds.found, "a screen-space text renderer should reach the overlay pass"))
    {
        return false;
    }
    std::cerr << "  screen text at (" << TextX << ", " << TextY << ") covers ["
              << bounds.left << ", " << bounds.top << "] to [" << bounds.right << ", "
              << bounds.bottom << "] in a " << ViewWidth << "x" << ViewHeight << " view"
              << std::endl;

    // 잉크는 지정 원점의 왼쪽·위로 1픽셀 이상 나가지 않고 bearing 여백 6픽셀 안에서 시작해야 한다.
    // 텍스트 블록 중심을 원점에 놓아 폭의 절반만큼 밀리는 배치를 구분한다.
    constexpr float Bearing = 6.0f;
    const bool startsWhereItWasPut = bounds.left >= TextX - 1.0f &&
        bounds.left - TextX <= Bearing && bounds.top >= TextY - 1.0f &&
        bounds.top - TextY <= Bearing;
    // 글리프가 뷰 안에 놓이는지도 직접 확인한다.
    const bool insideTheView = bounds.left >= 0.0f && bounds.top >= 0.0f &&
        bounds.right <= ViewWidth && bounds.bottom <= ViewHeight;

    return Expect(
               startsWhereItWasPut,
               "screen-space text should begin at the position it was given, not centre on it") &&
        Expect(insideTheView, "screen-space text placed inside the view should stay inside it");
}

static const TestSupport::Registration gGameViewTextClipTests{
    "RenderFrame", "game view text clip tests should pass", RunGameViewTextClipTests };
