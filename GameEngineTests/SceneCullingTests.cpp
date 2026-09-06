#include "SceneCullingTests.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "Assets/AssetReference.h"
#include "Math/Color.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Platform/PlatformServices.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Rendering/TextRasterizationCache.h"
#include "Runtime/Camera.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "SceneRendering/SceneRenderPass.h"

#include "TestSupport.h"

using namespace GameEngine;
using TestSupport::Expect;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{

// 1x1 RGBA PNG다. 컬링이 재는 것은 상자의 겹침뿐이라 픽셀이 무엇인지는 상관없지만, 스프라이트가
// 텍스처를 얻지 못하면 draw 자체가 실리지 않으므로 진짜 이미지여야 한다.
constexpr unsigned char OnePixelPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
    0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
    0x0F, 0x04, 0x00, 0x09, 0xFB, 0x03, 0xFD, 0xFB, 0x5E, 0x6B, 0x2B, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

constexpr Math::Color InsideTint{ 1.0f, 0.0f, 0.0f, 1.0f };
constexpr Math::Color OutsideTint{ 0.0f, 0.0f, 1.0f, 1.0f };
constexpr Math::Color StraddlerTint{ 0.0f, 1.0f, 0.0f, 1.0f };
constexpr Math::Color ScreenTint{ 1.0f, 1.0f, 1.0f, 1.0f };

/// <summary>
/// 정육각형 조각(4×4 세계 단위, 반폭 2)을 그리는 스프라이트다. sliced로 두는 이유는 크기를
/// SetSize로 직접 선언할 수 있어서다 — 텍스처 픽셀과 pixelsPerUnit을 거치지 않으므로 상자의
/// 세계 단위 크기가 시험 코드에 그대로 드러난다.
/// </summary>
Runtime::GameObject* AddWorldSprite(
    Runtime::Scene& scene, const char* const name, const Math::Vector3& position,
    const Math::Color& tint)
{
    Runtime::GameObject* const object = scene.CreateGameObject(name);
    object->GetTransform().SetPosition(position);
    Runtime::SpriteRenderer* const renderer = object->AddComponent<Runtime::SpriteRenderer>();
    renderer->SetSprite(Assets::AssetReference::Parse("Sprites/tile.png"));
    renderer->SetDrawMode(Runtime::SpriteRenderer::DrawMode::Sliced);
    renderer->SetSize({ 4.0f, 4.0f });
    renderer->SetColor(tint);
    return object;
}

/// <summary>화면 공간 스프라이트다. RectTransform 없이도 그려지며, 카메라와 무관해야 한다.</summary>
Runtime::GameObject* AddScreenSprite(Runtime::Scene& scene, const char* const name)
{
    Runtime::GameObject* const object = scene.CreateGameObject(name);
    Runtime::SpriteRenderer* const renderer = object->AddComponent<Runtime::SpriteRenderer>();
    renderer->SetSprite(Assets::AssetReference::Parse("Sprites/tile.png"));
    renderer->SetSpace(Runtime::SpriteRenderer::Space::Screen);
    renderer->SetColor(ScreenTint);
    return object;
}

/// <summary>
/// 월드 공간 글자다. 스프라이트와 상자를 얻는 자리가 전혀 달라서(ITextMeasure가 잰 블록
/// 폭·높이) 따로 잰다 — 스프라이트의 IsVisible 호출이 맞다고 해서 글자 쪽 로컬 상자 계산이
/// 맞다는 보장은 없다.
/// </summary>
Runtime::GameObject* AddWorldText(
    Runtime::Scene& scene, const char* const name, const Math::Vector3& position)
{
    Runtime::GameObject* const object = scene.CreateGameObject(name);
    object->GetTransform().SetPosition(position);
    Runtime::TextRenderer* const renderer = object->AddComponent<Runtime::TextRenderer>();
    renderer->SetText("Hi");
    renderer->SetSpace(Runtime::TextRenderer::Space::World);
    // 픽셀 단위 글꼴을 세계 단위로 크게 눌러, 블록이 카메라의 10×10 세계 단위 시야 안에
    // 뚜렷이 들어오게 한다.
    renderer->SetPixelsPerUnit(50.0f);
    return object;
}

/// <summary>테두리 살아남은 draw들의 틴트다. 어느 오브젝트가 걸러졌는지 이것으로 가른다.</summary>
[[nodiscard]] std::vector<Math::Color> WorldSpriteTints(const Rendering::RenderFrame& frame)
{
    std::vector<Math::Color> tints;
    for (const Rendering::SpriteDraw* const draw :
         frame.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Transparent))
    {
        if (draw)
        {
            tints.push_back(draw->tint);
        }
    }
    return tints;
}

/// <summary>
/// 살아남은 월드 공간 글자 draw들의 x 위치다. 한 문자열이 여러 아틀라스 페이지에 걸치면
/// draw가 여럿일 수 있어(정확한 개수는 글꼴의 몫이다) 있는 그대로 목록으로 남기고, 「그 근처에
/// 하나라도 있는가」로 어느 오브젝트가 살아남았는지 가른다.
/// </summary>
[[nodiscard]] std::vector<float> WorldTextTranslationsX(const Rendering::RenderFrame& frame)
{
    std::vector<float> translations;
    for (const Rendering::TextDraw* const draw :
         frame.GetDraws<Rendering::TextDraw>(Rendering::RenderPass::Transparent))
    {
        if (draw)
        {
            translations.push_back(draw->localToWorld.GetTranslation().GetX());
        }
    }
    return translations;
}

[[nodiscard]] bool AnyNear(const std::vector<float>& values, const float target)
{
    for (const float value : values)
    {
        if (std::abs(value - target) < 1.0f)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool Contains(const std::vector<Math::Color>& tints, const Math::Color& tint)
{
    for (const Math::Color& candidate : tints)
    {
        if (candidate.r == tint.r && candidate.g == tint.g && candidate.b == tint.b)
        {
            return true;
        }
    }
    return false;
}

}

bool RunSceneCullingTests()
{
    std::cout << "running scene culling tests\n";
    bool passed = true;

    TemporaryDirectory projectDirectory("scene-culling");
    const std::filesystem::path root = projectDirectory.GetPath();
    const bool wrote =
        WriteFile(root / "Culling.gameproject", "{}") &&
        WriteFile(root / "Sprites" / "tile.png",
            std::string_view(reinterpret_cast<const char*>(OnePixelPng), sizeof(OnePixelPng))) &&
        WriteFile(root / "Sprites" / "tile.png.meta",
            R"({"format": "gameengine-meta/1", "pixelsPerUnit": 1.0})");
    if (!Expect(wrote, "the scene culling test project should be written"))
    {
        return false;
    }

    const Platform::DirectoryContentSource content(root);
    Runtime::Game game{ nullptr, nullptr };
    if (!Expect(game.Initialize(content, {}), "the scene culling test project should open"))
    {
        return false;
    }

    auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "Culling");
    Runtime::GameObject* const cameraObject = scene->CreateGameObject("Camera");
    Runtime::Camera* const camera = cameraObject->AddComponent<Runtime::Camera>();
    camera->SetProjectionMode(Runtime::Camera::ProjectionMode::Orthographic);
    camera->SetAspectRatio(1.0f);
    // 반높이 5, 반폭 5 — 카메라 위치를 중심으로 10×10 세계 단위를 본다.
    camera->SetOrthographicSize(5.0f);

    // 항상 카메라 안 — 원점에 있고 반폭 2다.
    AddWorldSprite(*scene, "Inside", { 0.0f, 0.0f, 0.0f }, InsideTint);
    // 카메라가 원점에 있을 때는 멀리 있어 밖이고, 카메라가 그쪽으로 가면 안이다.
    AddWorldSprite(*scene, "Outside", { 100.0f, 0.0f, 0.0f }, OutsideTint);
    // 카메라 반폭이 5일 때 오른쪽 경계는 x=5다. 반폭 2짜리가 x=5.5에 있으면 [3.5, 7.5]가
    // 카메라의 [-5, 5]와 [3.5, 5]에서 겹친다 — 걸친 것이지 완전히 밖이 아니다.
    AddWorldSprite(*scene, "Straddler", { 5.5f, 0.0f, 0.0f }, StraddlerTint);
    AddScreenSprite(*scene, "ScreenThing");
    // 스프라이트와 나란히 둔다. 상자를 얻는 계산이 완전히 다른 부류(글꼴이 잰 블록 폭·높이)
    // 라서, 스프라이트가 옳다고 글자도 옳다는 보장이 없다.
    AddWorldText(*scene, "TextInside", { 0.0f, -2.0f, 0.0f });
    AddWorldText(*scene, "TextOutside", { 100.0f, -2.0f, 0.0f });

    static_cast<void>(game.GetSceneManager().AddScene(std::move(scene)));

    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer)
    {
        std::cout << "  scene culling tests skipped: no bundled font on this machine\n";
        return true;
    }
    SceneRendering::SceneRenderPass frontend{
        std::make_shared<Rendering::TextRasterizationCache>(std::move(rasterizer)) };

    const auto collect = [&game, &frontend]() -> Rendering::RenderFrame
    {
        Rendering::RenderFrameBuilder builder;
        frontend.Collect(game, builder);
        return std::move(builder).Build();
    };

    // ---- ① 카메라가 원점에 있을 때: Inside와 Straddler는 보이고 Outside는 안 보인다 ----
    cameraObject->GetTransform().SetPosition({ 0.0f, 0.0f, 0.0f });
    {
        const Rendering::RenderFrame frame = collect();
        const std::vector<Math::Color> tints = WorldSpriteTints(frame);
        passed = Expect(
            tints.size() == 2, "a camera at the origin should see two of the three world sprites")
            && passed;
        passed = Expect(
            Contains(tints, InsideTint),
            "a sprite well inside the camera should be drawn") && passed;
        passed = Expect(
            Contains(tints, StraddlerTint),
            "a sprite straddling the camera's edge should be drawn, not culled away") && passed;
        passed = Expect(
            !Contains(tints, OutsideTint),
            "a sprite far outside the camera should not be drawn") && passed;
        const std::vector<float> textX = WorldTextTranslationsX(frame);
        passed = Expect(
            AnyNear(textX, 0.0f),
            "world-space text well inside the camera should be drawn") && passed;
        passed = Expect(
            !AnyNear(textX, 100.0f),
            "world-space text far outside the camera should not be drawn") && passed;
    }

    // ---- ② 카메라를 옮기면: 「무엇이 남았는가」가 바뀐다 — 수만 재면 못 잡는 결함이다 ----
    cameraObject->GetTransform().SetPosition({ 100.0f, 0.0f, 0.0f });
    {
        const Rendering::RenderFrame frame = collect();
        const std::vector<Math::Color> tints = WorldSpriteTints(frame);
        passed = Expect(
            tints.size() == 1, "a camera moved to the far sprite should again see exactly one")
            && passed;
        passed = Expect(
            Contains(tints, OutsideTint),
            "moving the camera should reveal the sprite it moved to") && passed;
        passed = Expect(
            !Contains(tints, InsideTint),
            "moving the camera away should cull the sprite it left") && passed;
        const std::vector<float> textX = WorldTextTranslationsX(frame);
        passed = Expect(
            AnyNear(textX, 100.0f),
            "moving the camera should reveal the text it moved to") && passed;
        passed = Expect(
            !AnyNear(textX, 0.0f),
            "moving the camera away should cull the text it left") && passed;
    }

    // ---- ③ 카메라를 넓히면 셋 다 보인다 ----
    cameraObject->GetTransform().SetPosition({ 50.0f, 0.0f, 0.0f });
    camera->SetOrthographicSize(60.0f);
    {
        const Rendering::RenderFrame frame = collect();
        passed = Expect(
            WorldSpriteTints(frame).size() == 3,
            "a wide enough camera should see every world sprite") && passed;
        const std::vector<float> textX = WorldTextTranslationsX(frame);
        passed = Expect(
            AnyNear(textX, 0.0f) && AnyNear(textX, 100.0f),
            "a wide enough camera should see both texts") && passed;
    }
    camera->SetOrthographicSize(5.0f);
    cameraObject->GetTransform().SetPosition({ 0.0f, 0.0f, 0.0f });

    // ---- ④ 화면 공간은 카메라와 무관하다 ----
    {
        const Rendering::RenderFrame frame = collect();
        const std::vector<const Rendering::SpriteDraw*> overlay =
            frame.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Overlay);
        bool sawScreenThing = false;
        for (const Rendering::SpriteDraw* const draw : overlay)
        {
            if (draw && draw->tint.r == ScreenTint.r && draw->tint.g == ScreenTint.g)
            {
                sawScreenThing = true;
            }
        }
        passed = Expect(
            sawScreenThing,
            "a screen-space sprite should be drawn regardless of the camera") && passed;
    }
    cameraObject->GetTransform().SetPosition({ 100.0f, 0.0f, 0.0f });
    {
        const Rendering::RenderFrame frame = collect();
        const std::vector<const Rendering::SpriteDraw*> overlay =
            frame.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Overlay);
        bool sawScreenThing = false;
        for (const Rendering::SpriteDraw* const draw : overlay)
        {
            if (draw && draw->tint.r == ScreenTint.r && draw->tint.g == ScreenTint.g)
            {
                sawScreenThing = true;
            }
        }
        passed = Expect(
            sawScreenThing,
            "a screen-space sprite should still be drawn after the camera moves away")
            && passed;
    }
    cameraObject->GetTransform().SetPosition({ 0.0f, 0.0f, 0.0f });

    // ---- ⑤ 원근 카메라는 오늘 아무것도 거르지 않는다 ----
    //
    // 절두체가 없어(직교만 안다) 거르면 틀린 상자로 거르는 버그가 된다. 카메라를 원점에 두고
    // Outside만 멀리 있는 채로 원근으로 바꾸면, ①에서는 사라졌던 그것이 다시 보여야 한다 —
    // 이 갈래가 조용히 죽지 않았다는 증거다.
    camera->SetProjectionMode(Runtime::Camera::ProjectionMode::Perspective);
    {
        const Rendering::RenderFrame frame = collect();
        passed = Expect(
            WorldSpriteTints(frame).size() == 3,
            "a perspective camera should cull nothing, even the far sprite") && passed;
        const std::vector<float> textX = WorldTextTranslationsX(frame);
        passed = Expect(
            AnyNear(textX, 0.0f) && AnyNear(textX, 100.0f),
            "a perspective camera should cull no text either") && passed;
    }
    camera->SetProjectionMode(Runtime::Camera::ProjectionMode::Orthographic);

    return passed;
}

static const TestSupport::Registration gSceneCullingTests{
    "RenderFrame", "scene culling tests should pass", RunSceneCullingTests };
