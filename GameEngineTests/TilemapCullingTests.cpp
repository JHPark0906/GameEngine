#include "TilemapCullingTests.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "Assets/AssetReference.h"
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
#include "Runtime/TilemapRenderer.h"
#include "Runtime/Transform.h"
#include "SceneRendering/SceneRenderPass.h"

#include "TestSupport.h"

using namespace GameEngine;
using TestSupport::Expect;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{

constexpr unsigned char OnePixelPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
    0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
    0x0F, 0x04, 0x00, 0x09, 0xFB, 0x03, 0xFD, 0xFB, 0x5E, 0x6B, 0x2B, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

/// <summary>실린 칸들의 (열, 행) 집합이다. 어느 draw인지는 상관없다 — 이 시험에는 타일맵 하나뿐이다.</summary>
[[nodiscard]] std::set<std::pair<int, int>> DrawnCells(const Rendering::RenderFrame& frame)
{
    std::set<std::pair<int, int>> cells;
    for (const Rendering::TilemapDraw* const draw :
         frame.GetDraws<Rendering::TilemapDraw>(Rendering::RenderPass::Transparent))
    {
        if (!draw || !draw->tiles)
        {
            continue;
        }
        for (const Rendering::TilemapTile& tile : *draw->tiles)
        {
            cells.emplace(tile.column, tile.row);
        }
    }
    return cells;
}

}

bool RunTilemapCullingTests()
{
    std::cout << "running tilemap culling tests\n";
    bool passed = true;

    TemporaryDirectory projectDirectory("tilemap-culling");
    const std::filesystem::path root = projectDirectory.GetPath();
    const bool wrote =
        WriteFile(root / "TilemapCulling.gameproject", "{}") &&
        WriteFile(root / "Sprites" / "tile.png",
            std::string_view(reinterpret_cast<const char*>(OnePixelPng), sizeof(OnePixelPng))) &&
        WriteFile(root / "Sprites" / "tile.png.meta",
            R"({"format": "gameengine-meta/1", "pixelsPerUnit": 1.0})");
    if (!Expect(wrote, "the tilemap culling test project should be written"))
    {
        return false;
    }

    const Platform::DirectoryContentSource content(root);
    Runtime::Game game{ nullptr, nullptr };
    if (!Expect(game.Initialize(content, {}), "the tilemap culling test project should open"))
    {
        return false;
    }

    auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "TilemapCulling");
    Runtime::GameObject* const cameraObject = scene->CreateGameObject("Camera");
    Runtime::Camera* const camera = cameraObject->AddComponent<Runtime::Camera>();
    camera->SetProjectionMode(Runtime::Camera::ProjectionMode::Orthographic);
    camera->SetAspectRatio(1.0f);

    // 5×5 격자, 칸 하나가 세계 단위 1×1. 전부 채운다 — 어느 칸이 실렸는지만 보면 되므로
    // 타일 값 자체는 뜻이 없다.
    Runtime::GameObject* const tilemapObject = scene->CreateGameObject("Tilemap");
    Runtime::TilemapRenderer* const tilemap = tilemapObject->AddComponent<Runtime::TilemapRenderer>();
    tilemap->SetTileset(Assets::AssetReference::Parse("Sprites/tile.png"));
    tilemap->SetColumns(5);
    tilemap->SetRows(5);
    tilemap->SetCellSize({ 1.0f, 1.0f });
    for (int row = 0; row < 5; ++row)
    {
        for (int column = 0; column < 5; ++column)
        {
            static_cast<void>(tilemap->SetTile(column, row, 0));
        }
    }

    static_cast<void>(game.GetSceneManager().AddScene(std::move(scene)));

    SceneRendering::SceneRenderPass frontend{
        std::make_shared<Rendering::TextRasterizationCache>(
            Platform::PlatformServices::CreateTextRasterizer()) };

    const auto collect = [&game, &frontend]() -> Rendering::RenderFrame
    {
        Rendering::RenderFrameBuilder builder;
        frontend.Collect(game, builder);
        return std::move(builder).Build();
    };

    // ---- ① 카메라가 격자의 한구석 2×2만 덮으면 그 네 칸만 실린다 ----
    //
    // 반높이·반폭 0.9, 중심 (1,1) → 시야가 [0.1, 1.9]×[0.1, 1.9]다. 칸 경계(정수)에 걸치지
    // 않게 일부러 0.9를 썼다 — 경계에 정확히 닿으면 어느 쪽 칸까지 포함할지가 이 시험이 재려는
    // 것과 다른 문제(경계 반올림)가 되어 버린다.
    camera->SetOrthographicSize(0.9f);
    cameraObject->GetTransform().SetPosition({ 1.0f, 1.0f, 0.0f });
    {
        const Rendering::RenderFrame frame = collect();
        const std::set<std::pair<int, int>> cells = DrawnCells(frame);
        const std::set<std::pair<int, int>> expected{ { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } };
        passed = Expect(
            cells == expected,
            "a camera over one corner of the grid should draw only that corner's four cells")
            && passed;
    }

    // ---- ② 카메라를 옮기면: 실린 칸의 집합 자체가 바뀐다 ----
    cameraObject->GetTransform().SetPosition({ 4.0f, 4.0f, 0.0f });
    {
        const Rendering::RenderFrame frame = collect();
        const std::set<std::pair<int, int>> cells = DrawnCells(frame);
        const std::set<std::pair<int, int>> expected{ { 3, 3 }, { 3, 4 }, { 4, 3 }, { 4, 4 } };
        passed = Expect(
            cells == expected,
            "moving the camera to the opposite corner should draw that corner instead") && passed;
    }

    // ---- ③ 카메라가 격자를 완전히 벗어나면 draw 자체가 없다 ----
    cameraObject->GetTransform().SetPosition({ 100.0f, 100.0f, 0.0f });
    {
        const Rendering::RenderFrame frame = collect();
        passed = Expect(
            DrawnCells(frame).empty(),
            "a camera entirely outside the grid should draw no cells at all") && passed;
    }

    // ---- ④ 카메라가 격자 전체를 덮으면 스물다섯 칸 전부 실린다 ----
    camera->SetOrthographicSize(10.0f);
    cameraObject->GetTransform().SetPosition({ 2.5f, 2.5f, 0.0f });
    {
        const Rendering::RenderFrame frame = collect();
        passed = Expect(
            DrawnCells(frame).size() == 25,
            "a camera covering the whole grid should draw every cell") && passed;
    }

    // ---- ⑤ 원근 카메라는 오늘 아무것도 거르지 않는다 — 훑는 범위도 좁히지 않는다 ----
    camera->SetProjectionMode(Runtime::Camera::ProjectionMode::Perspective);
    cameraObject->GetTransform().SetPosition({ 1.0f, 1.0f, 0.0f });
    {
        const Rendering::RenderFrame frame = collect();
        passed = Expect(
            DrawnCells(frame).size() == 25,
            "a perspective camera should sweep the whole grid, not just the corner it sits near")
            && passed;
    }

    return passed;
}

static const TestSupport::Registration gTilemapCullingTests{
    "RenderFrame", "tilemap culling tests should pass", RunTilemapCullingTests };
