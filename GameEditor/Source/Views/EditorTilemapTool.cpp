#include "Views/EditorTilemapTool.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "Document/EditorContext.h"
#include "Views/EditorDockLayout.h"
#include "Rules/EditorPanelCommon.h"
#include "Diagnostics/Debug.h"
#include "Math/Matrix.h"
#include "Runtime/GameObject.h"
#include "Runtime/TilemapRenderer.h"
#include "Runtime/Transform.h"
#include "Assets/Asset.h"
#include "Assets/AssetDatabase.h"
#include "Assets/TextureData.h"

namespace GameEditor
{

namespace
{
    /// <summary>팔레트 칸 하나의 논리 크기다. 타일 그림이 이 정사각형을 채운다.</summary>
    constexpr float PaletteCellSize = 34.0f;
}

TilePaintCommand::TilePaintCommand(
    EditorContext& context, const unsigned int componentId, std::vector<Change> changes)
    : mContext(&context), mComponentId(componentId), mChanges(std::move(changes))
{
}

bool TilePaintCommand::Apply()
{
    return Write(true);
}

bool TilePaintCommand::Revert()
{
    return Write(false);
}

bool TilePaintCommand::Write(const bool useAfter) const
{
    auto* const tilemap = dynamic_cast<GameEngine::Runtime::TilemapRenderer*>(
        mContext->FindObject(mComponentId));
    if (!tilemap)
    {
        GameEngine::Diagnostics::Debug::LogWarning(
            "A tile paint edit no longer resolves to a tilemap. component=", mComponentId);
        return false;
    }

    // 배열 자리로 되돌린다: 획을 기록한 뒤 격자 크기가 바뀌었으면 자리가 사라졌을 수 있으므로,
    // 지금 크기 밖의 기록은 조용히 지나친다 — 되돌릴 수 있는 만큼은 되돌린다.
    std::vector<int> tiles = tilemap->GetTiles();
    for (const Change& change : mChanges)
    {
        if (change.arrayIndex >= 0 &&
            static_cast<std::size_t>(change.arrayIndex) < tiles.size())
        {
            tiles[static_cast<std::size_t>(change.arrayIndex)] =
                useAfter ? change.after : change.before;
        }
    }
    tilemap->SetTiles(std::move(tiles));
    return true;
}

EditorTilemapTool::EditorTilemapTool(EditorContext& context)
    : mContext(context)
{
}

GameEngine::Runtime::TilemapRenderer* EditorTilemapTool::FindSelectedTilemap() const
{
    auto* const selected =
        dynamic_cast<GameEngine::Runtime::GameObject*>(mContext.GetSelectedObject());
    return selected ? selected->GetComponent<GameEngine::Runtime::TilemapRenderer>() : nullptr;
}

void EditorTilemapTool::DrawPalette(
    GameEngine::UI::UIContext& ui, const GameEngine::UI::UIRect& content)
{
    const float scale = ui.GetScale();
    const float rowHeight = RowHeight * scale;
    const float padding = Padding * scale;

    GameEngine::Runtime::TilemapRenderer* const selectedTilemap = FindSelectedTilemap();
    if (!selectedTilemap)
    {
        // 자기 패널은 선택과 함께 사라지지 않으므로 비어 있는 이유를 말한다.
        ui.DrawLabel(
            { content.x + padding, content.y + padding, content.width - 2.0f * padding,
              rowHeight },
            "Select an object with a Tilemap Renderer to paint.", DimTextColor, SecondaryFontSize);
        return;
    }
    GameEngine::Runtime::TilemapRenderer& tilemap = *selectedTilemap;

    float y = content.y;
    const GameEngine::UI::WidgetId toolId =
        GameEngine::UI::MakeWidgetId("tilemap-tool", tilemap.GetInstanceId());

    const auto rowVisible = [&content, rowHeight](const float top)
    {
        // 인스펙터가 자기 속성 행에 쓰는 것과 같은 클리핑이다. 즉시 모드 UI에는 잘라내기가
        // 없으므로, 패널을 벗어난 줄은 그리지 않는 것이 곧 잘라내기다 — 그리기만 넘치는 것이
        // 아니라 그 자리의 클릭을 위 패널이 가져가므로, 보이는데 눌리지 않는 칸이 생긴다.
        return top >= content.y && top + rowHeight <= content.GetBottom();
    };

    // 팔레트 이름은 패널 제목줄에 있으므로 안쪽에 반복하지 않는다.
    // 추가 제목줄 대신 타일 격자가 그 높이를 사용한다.
    y += padding;

    // 페인팅 스위치와 붓/지우개. 페인팅이 켜져 있는 동안 씬 뷰의 왼쪽 드래그는 궤도가 아니라
    // 붓이므로, 켜고 끄는 것이 눈에 보여야 한다.
    const float halfWidth = (content.width - 3.0f * padding) * 0.5f;
    if (rowVisible(y))
    {
        if (ui.DrawButton(
                GameEngine::UI::MakeChildWidgetId(toolId, 1),
                { content.x + padding, y, halfWidth, rowHeight },
                mPaintingEnabled ? "Painting: On" : "Painting: Off"))
        {
            mPaintingEnabled = !mPaintingEnabled;
        }
        if (ui.DrawButton(
                GameEngine::UI::MakeChildWidgetId(toolId, 2),
                { content.x + 2.0f * padding + halfWidth, y, halfWidth, rowHeight },
                mErasing ? "Eraser" : "Brush"))
        {
            mErasing = !mErasing;
        }
    }
    y += rowHeight + 2.0f * scale;

    // 타일셋이 몇 칸인지도, 각 칸이 이미지의 어디인지도 에셋이 답한다. 물을 길이 없으면 상한까지
    // 번호를 늘어놓는 수밖에 없고, 그러면 고르는 사람이 그림이 아니라 숫자를 보게 되며 상한을
    // 넘는 칸에는 닿지도 못한다.
    const GameEngine::Assets::Sprite* tileset = nullptr;
    std::shared_ptr<const GameEngine::Assets::TextureData> tilesetTexture;
    if (const GameEngine::Assets::AssetDatabase* const assets =
            mContext.GetProjectAssetDatabase())
    {
        tileset = assets->FindAsset<GameEngine::Assets::Sprite>(tilemap.GetTileset());
        if (tileset)
        {
            tilesetTexture = assets->LoadTexture(tilemap.GetTileset());
        }
    }
    const int tileCount = tileset ? tileset->GetSheet().GetFrameCount() : 0;
    if (tileCount <= 0)
    {
        if (rowVisible(y))
        {
            ui.DrawLabel(
                { content.x + padding, y, content.width - 2.0f * padding, rowHeight },
                "Assign a tileset to paint.", DimTextColor, SecondaryFontSize);
        }
        y += rowHeight + 2.0f * scale;
        return;
    }

    // 칸은 정사각형이고 크기가 고정이다. 한 줄에 몇 개가 들어가는지는 패널 너비가 정한다 —
    // 팔레트 패널을 넓히면 더 많이 보이고, 좁혀도 칸이 찌그러지지 않는다.
    const float cellSize = PaletteCellSize * scale;
    const float availableWidth = content.width - 2.0f * padding;
    const int columns = (std::max)(1, static_cast<int>(availableWidth / cellSize));
    const int rows = (tileCount + columns - 1) / columns;

    // 팔레트는 패널에 남은 자리를 그대로 쓴다. 높이를 상수로 묶으면 그 상한이 곧 두 번째
    // 스크롤이 된다: 256칸짜리 타일셋을 여섯 줄 창으로 보면 바깥 스크롤 안에서 또 스크롤해야
    // 한다. 자기 패널에서는 묶을 이유가 없고, 넘칠 때만 이 상자가 스크롤하므로 스크롤은 한
    // 겹이다.
    const float boxHeight =
        ComputePaletteBoxHeight(content.GetBottom() - y - padding, rows, cellSize);
    const GameEngine::UI::UIRect box{ content.x + padding, y, availableWidth, boxHeight };
    const float offset = ui.ApplyScroll(
        GameEngine::UI::MakeChildWidgetId(toolId, 3), box,
        static_cast<float>(rows) * cellSize);

    for (int row = 0; row < rows; ++row)
    {
        const float top = box.y + static_cast<float>(row) * cellSize - offset;
        // 상자 안이면서 패널 안인 줄만 그린다.
        if (top < box.y || top + cellSize > box.GetBottom() ||
            top < content.y || top + cellSize > content.GetBottom())
        {
            continue;
        }
        for (int column = 0; column < columns; ++column)
        {
            const int tile = row * columns + column;
            if (tile >= tileCount)
            {
                break;
            }
            const GameEngine::UI::UIRect cell{
                box.x + static_cast<float>(column) * cellSize, top,
                cellSize - 2.0f * scale, cellSize - 2.0f * scale };

            // 선택과 호버는 칸 바탕에 깔고 타일 그림은 그 안쪽에 그린다. 그래야 불투명한 타일도
            // 테두리 띠로 선택이 보이고, 그림이 호버 색에 덮이지 않는다.
            const bool selected = !mErasing && mSelectedTile == tile;
            const GameEngine::UI::UIContext::SelectableResult result = ui.DrawSelectable(
                GameEngine::UI::MakeChildWidgetId(
                    toolId, static_cast<std::uint64_t>(100 + tile)),
                cell, "", selected);
            const float inset = 3.0f * scale;
            float u = 0.0f;
            float v = 0.0f;
            float width = 1.0f;
            float height = 1.0f;
            tileset->GetFrameRect(tile, u, v, width, height);
            ui.DrawImageRegion(
                { cell.x + inset, cell.y + inset,
                  cell.width - 2.0f * inset, cell.height - 2.0f * inset },
                tilesetTexture, { u, v, width, height });
            if (result.clicked)
            {
                mSelectedTile = tile;
                mErasing = false;
            }
        }
    }
    y += boxHeight + 2.0f * scale;
}

SceneInputCapture EditorTilemapTool::HandleSceneInput(const SceneToolInput& input)
{
    // 붓이 쓰는 것은 왼쪽 버튼뿐이다. 가운데 드래그의 팬과 휠의 돌리는 손대지 않으므로, 칠하는
    // 동안에도 씬 뷰가 평소대로 쓴다.
    SceneInputCapture brush;
    brush.leftButton = true;

    GameEngine::Runtime::TilemapRenderer* const tilemap = FindSelectedTilemap();
    if (!mPaintingEnabled || !tilemap)
    {
        // 도구가 비켜서면 진행 중이던 획은 여기서 닫힌다 — 선택이 바뀌어도 획이 어중간하게
        // 남지 않는다.
        FinishStroke();
        return {};
    }
    if (!input.leftDragging)
    {
        FinishStroke();
        // 페인팅 중에는 왼쪽 드래그를 도구가 가져간다: 떼는 프레임까지 가져가야 씬 뷰가 그
        // 뗌을 클릭 피킹으로 읽지 않는다.
        return brush;
    }

    GameEngine::Runtime::Transform* const transform = tilemap->GetTransform();
    if (!transform)
    {
        return brush;
    }

    // 타일맵은 자기 로컬 z=0 평면에 눕는다. 커서의 광선을 그 평면과 만나게 해 로컬 좌표를 얻고,
    // 그 좌표가 어느 칸인지는 타일맵이 답한다.
    GameEngine::Math::Matrix4x4 worldToLocal;
    if (!transform->GetLocalToWorldMatrix().TryInvert(worldToLocal))
    {
        return brush;
    }
    const GameEngine::Math::Vector3 localOrigin = worldToLocal.TransformPoint(input.rayOrigin);
    const GameEngine::Math::Vector3 localDirection =
        worldToLocal.TransformDirection(input.rayDirection);
    if (std::abs(localDirection.GetZ()) < 1e-6f)
    {
        return brush;
    }
    const float distance = -localOrigin.GetZ() / localDirection.GetZ();
    if (distance <= 0.0f)
    {
        return brush;
    }
    const GameEngine::Math::Vector3 hit = localOrigin + localDirection * distance;

    int column = 0;
    int row = 0;
    if (!tilemap->TryGetCellAt(hit.GetX(), hit.GetY(), column, row))
    {
        return brush;
    }

    const int arrayIndex = tilemap->GetTileArrayIndex(column, row);
    const int before = tilemap->GetTile(column, row);
    const int after = mErasing ? GameEngine::Runtime::TilemapRenderer::EmptyTile : mSelectedTile;
    if (mStrokeComponentId != tilemap->GetInstanceId())
    {
        FinishStroke();
        mStrokeComponentId = tilemap->GetInstanceId();
    }
    if (arrayIndex < 0 || !tilemap->SetTile(column, row, after))
    {
        return brush;
    }

    // 한 획이 같은 칸을 여러 번 지나도 기록은 한 번이다: 되돌릴 때 필요한 것은 획이 시작하기
    // 전의 값뿐이다.
    const auto recorded = std::ranges::find_if(
        mStroke, [arrayIndex](const TilePaintCommand::Change& change)
        { return change.arrayIndex == arrayIndex; });
    if (recorded != mStroke.end())
    {
        recorded->after = after;
    }
    else
    {
        mStroke.push_back({ arrayIndex, before, after });
    }
    return brush;
}

void EditorTilemapTool::FinishStroke()
{
    if (mStroke.empty() || mStrokeComponentId == 0)
    {
        mStroke.clear();
        mStrokeComponentId = 0;
        return;
    }

    // 획은 이미 장면에 칠해져 있으므로 Apply 없이 편집을 기록한다.
    // 인스펙터와 같은 기록 경로가 Play 중 기록 여부를 판단해, 붓질도 같은 정책을 따른다.
    mContext.RecordEdit(
        std::make_unique<TilePaintCommand>(mContext, mStrokeComponentId, std::move(mStroke)));
    mStroke.clear();
    mStrokeComponentId = 0;
}


}
