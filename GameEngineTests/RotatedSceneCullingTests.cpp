#include "RotatedSceneCullingTests.h"

#include <memory>
#include <set>
#include <string_view>
#include <utility>

#include "Assets/AssetReference.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Camera.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/TilemapRenderer.h"
#include "Runtime/Transform.h"
#include "SceneRendering/SceneRenderPass.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    constexpr unsigned char OnePixelPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0x0F, 0x04, 0x00, 0x09, 0xFB, 0x03, 0xFD, 0xFB, 0x5E, 0x6B, 0x2B, 0x00, 0x00, 0x00, 0x00,
        0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
}

bool RunRotatedSceneCullingTests()
{
    using TestSupport::Expect;
    TestSupport::TemporaryDirectory directory("rotated-scene-culling");
    const auto root = directory.GetPath();
    if (!TestSupport::WriteFile(root / "Culling.gameproject", "{}") ||
        !TestSupport::WriteFile(root / "tile.png", std::string_view(
            reinterpret_cast<const char*>(OnePixelPng), sizeof(OnePixelPng))) ||
        !TestSupport::WriteFile(root / "tile.png.meta",
            R"({"format":"gameengine-meta/1","pixelsPerUnit":1.0})")) return false;
    Platform::DirectoryContentSource content(root);
    Runtime::Game game{ nullptr, nullptr };
    if (!Expect(game.Initialize(content, {}), "the rotated camera fixture opens")) return false;
    auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "Rotated camera");
    auto* cameraObject = scene->CreateGameObject("Camera");
    auto* camera = cameraObject->AddComponent<Runtime::Camera>();
    camera->SetOrthographicSize(1.9f);
    camera->SetAspectRatio(4.0f);
    camera->SetProjectionMode(Runtime::Camera::ProjectionMode::Orthographic);
    auto* parent = scene->CreateGameObject("Camera parent");
    const auto addSprite = [&](const Math::Vector3& position, const Math::Color& color)
    {
        auto* object = scene->CreateGameObject("Marker");
        object->GetTransform().SetPosition(position);
        auto* renderer = object->AddComponent<Runtime::SpriteRenderer>();
        renderer->SetSprite(Assets::AssetReference::Parse("tile.png"));
        renderer->SetColor(color);
    };
    addSprite({ 6, 0, 0 }, { 1, 0, 0, 1 });
    addSprite({ 0, 6, 0 }, { 0, 1, 0, 1 });
    addSprite({ 0, 12, 0 }, { 0, 0, 1, 1 });
    auto* mapObject = scene->CreateGameObject("Map");
    mapObject->GetTransform().SetPosition({ -8, -8, 0 });
    auto* map = mapObject->AddComponent<Runtime::TilemapRenderer>();
    map->SetTileset(Assets::AssetReference::Parse("tile.png"));
    map->SetColumns(16);
    map->SetRows(16);
    for (int row = 0; row < 16; ++row)
    for (int column = 0; column < 16; ++column) static_cast<void>(map->SetTile(column, row, 0));
    static_cast<void>(game.GetSceneManager().AddScene(std::move(scene)));
    SceneRendering::SceneRenderPass frontend{ nullptr };
    const auto collect = [&]
    {
        Rendering::RenderFrameBuilder builder;
        frontend.Collect(game, builder);
        return std::move(builder).Build();
    };
    const auto original = collect();
    const auto originalSprites = original.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Transparent);
    bool passed = Expect(originalSprites.size() == 1 && originalSprites.front()->tint.r == 1,
        "an unrotated wide camera sees the horizontal marker at the existing 2D depth");
    cameraObject->GetTransform().SetRotation({ 0, 0, 90 });
    const auto rotated = collect();
    const auto rotatedSprites = rotated.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Transparent);
    passed &= Expect(rotatedSprites.size() == 1 && rotatedSprites.front()->tint.g == 1,
        "rotating the scene camera reveals the vertical marker and culls the horizontal marker");
    std::set<std::pair<int, int>> actual;
    for (const auto* draw : rotated.GetDraws<Rendering::TilemapDraw>(Rendering::RenderPass::Transparent))
        for (const auto& tile : *draw->tiles) actual.emplace(tile.column, tile.row);
    std::set<std::pair<int, int>> expected;
    for (int row = 0; row < 16; ++row)
    for (int column = 6; column <= 9; ++column) expected.emplace(column, row);
    passed &= Expect(actual == expected, "tile traversal uses the rotated camera's vertical strip");
    cameraObject->GetTransform().SetRotation({ 0, 0, 0 });
    parent->GetTransform().SetRotation({ 0, 0, 90 });
    parent->GetTransform().SetScale({ 2, 1, 1 });
    static_cast<void>(cameraObject->GetTransform().SetParent(&parent->GetTransform()));
    const auto inherited = collect();
    const auto inheritedSprites = inherited.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Transparent);
    passed &= Expect(inheritedSprites.size() == 2,
        "inherited camera rotation and nonuniform scale also expand the correct view axis");
    return passed;
}

static const TestSupport::Registration gRotatedSceneCullingTests{
    "RenderFrame", "rotated scene cameras preserve visible sprites and tile ranges", RunRotatedSceneCullingTests };
