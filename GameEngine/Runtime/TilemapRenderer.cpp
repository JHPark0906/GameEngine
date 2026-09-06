#include "pch.h"
#include "TilemapRenderer.h"

#include "PropertyDescriptor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>격자 한 변의 상한이다. 실수로 적힌 큰 수가 장면 하나로 메모리를 삼키지 않게 한다.</summary>
    constexpr int MaximumGridSide = 1024;

    /// <summary>
    /// TilemapRenderer가 선언하는 속성들이다. 타일 배열은 값 하나가 아니라 구조라서 여기 없다 —
    /// 그것은 컴포넌트가 직접 쓰고 읽는 layers 목록이다.
    /// </summary>
    std::span<const PropertyDescriptor> TilemapRendererProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeAssetProperty<TilemapRenderer>(
                "tileset", "Tileset", Assets::AssetType::Sprite,
                &TilemapRenderer::GetTileset, &TilemapRenderer::SetTileset,
                PropertyTraits::OmitWhenInvalid),
            MakeProperty<TilemapRenderer>(
                "columns", "Columns", &TilemapRenderer::GetColumns, &TilemapRenderer::SetColumns),
            MakeProperty<TilemapRenderer>(
                "rows", "Rows", &TilemapRenderer::GetRows, &TilemapRenderer::SetRows),
            MakeProperty<TilemapRenderer>(
                "cellSize", "Cell Size",
                &TilemapRenderer::GetCellSize, &TilemapRenderer::SetCellSize),
            MakeProperty<TilemapRenderer>(
                "color", "Color", &TilemapRenderer::GetColor, &TilemapRenderer::SetColor),
        };
        return properties;
    }
}

const ComponentType& TilemapRenderer::StaticType()
{
    static const ComponentType type{
        "TilemapRenderer", &Renderer::StaticType(), &TilemapRendererProperties,
        &MakeComponentInstance<TilemapRenderer> };
    return type;
}

void TilemapRenderer::SetTileset(Assets::AssetReference tileset)
{
    mTileset = std::move(tileset);
}

void TilemapRenderer::SetColumns(const int columns)
{
    mColumns = std::clamp(columns, 1, MaximumGridSide);
    ResizeTiles();
}

void TilemapRenderer::SetRows(const int rows)
{
    mRows = std::clamp(rows, 1, MaximumGridSide);
    ResizeTiles();
}

void TilemapRenderer::SetCellSize(const Math::Vector2& cellSize)
{
    mCellSize = {
        std::isfinite(cellSize.GetX()) ? (std::max)(cellSize.GetX(), 0.001f) : 1.0f,
        std::isfinite(cellSize.GetY()) ? (std::max)(cellSize.GetY(), 0.001f) : 1.0f };
}

int TilemapRenderer::GetTileArrayIndex(const int column, const int row) const
{
    if (column < 0 || row < 0 || column >= mColumns || row >= mRows)
    {
        return -1;
    }
    return row * mColumns + column;
}

int TilemapRenderer::GetTile(const int column, const int row) const
{
    const int index = GetTileArrayIndex(column, row);
    return index >= 0 ? mTiles[static_cast<std::size_t>(index)] : EmptyTile;
}

bool TilemapRenderer::SetTile(const int column, const int row, const int tile)
{
    const int index = GetTileArrayIndex(column, row);
    if (index < 0)
    {
        return false;
    }
    const int clamped = tile < 0 ? EmptyTile : tile;
    if (mTiles[static_cast<std::size_t>(index)] == clamped)
    {
        return false;
    }
    mTiles[static_cast<std::size_t>(index)] = clamped;
    return true;
}

void TilemapRenderer::SetTiles(std::vector<int> tiles)
{
    mTiles = std::move(tiles);
    ResizeTiles();
}

Math::Vector2 TilemapRenderer::GetCellCenter(const int column, const int row) const
{
    return {
        (static_cast<float>(column) + 0.5f) * mCellSize.GetX(),
        (static_cast<float>(row) + 0.5f) * mCellSize.GetY() };
}

bool TilemapRenderer::TryGetCellAt(
    const float localX, const float localY, int& column, int& row) const
{
    // 바닥 함수로 나눈다: 원점 왼쪽·아래의 좌표도 음수 칸으로 떨어져야 격자 밖이라고 말할 수
    // 있다. 정수 나눗셈은 0 쪽으로 잘라서 -0.5칸을 0칸이라고 답했을 것이다.
    column = static_cast<int>(std::floor(localX / mCellSize.GetX()));
    row = static_cast<int>(std::floor(localY / mCellSize.GetY()));
    return column >= 0 && row >= 0 && column < mColumns && row < mRows;
}

void TilemapRenderer::ResizeTiles()
{
    const std::size_t needed =
        static_cast<std::size_t>(mColumns) * static_cast<std::size_t>(mRows);
    mTiles.resize(needed, EmptyTile);
}

void TilemapRenderer::WriteExtraSerializedState(Core::Json::Object& members) const
{
    // 레이어는 이 판의 엔진이 하나만 그리지만, 목록으로 적는다: 나중에 레이어가 여럿이 되어도
    // 이미 저장된 장면이 그대로 읽히도록 자리를 비워 두는 것이다.
    Core::Json::Array tiles;
    tiles.reserve(mTiles.size());
    for (const int tile : mTiles)
    {
        tiles.emplace_back(static_cast<double>(tile));
    }
    Core::Json::Object layer;
    layer.emplace("tiles", Core::Json(std::move(tiles)));

    Core::Json::Array layers;
    layers.reserve(1 + mUnreadLayers.size());
    layers.emplace_back(std::move(layer));
    // 지원하지 않는 뒤의 레이어는 입력 JSON 그대로 저장한다. 첫 레이어만 그리는 엔진에서
    // 다층 장면을 열어 저장해도 나머지 레이어의 데이터는 보존해야 한다.
    layers.insert(layers.end(), mUnreadLayers.begin(), mUnreadLayers.end());
    members.emplace("layers", Core::Json(std::move(layers)));
}

void TilemapRenderer::ReadExtraSerializedState(const Core::Json& json)
{
    mUnreadLayers.clear();

    const Core::Json* const layers = json.Find("layers");
    if (!layers || !layers->IsArray() || layers->Size() == 0)
    {
        return;
    }

    // 첫 레이어만 그린다. 나머지는 PreservedComponent가 모르는 컴포넌트에 세운 원칙 그대로
    // 보존한다: 이 프로세스가 다룰 줄 모르는 데이터도 저장에서 살아남아야 한다.
    const Core::Json::Array& all = layers->AsArray();
    mUnreadLayers.assign(all.begin() + 1, all.end());

    const Core::Json& first = all.front();
    const Core::Json* const tiles = first.IsObject() ? first.Find("tiles") : nullptr;
    if (!tiles || !tiles->IsArray())
    {
        return;
    }

    std::vector<int> read;
    read.reserve(tiles->Size());
    for (std::size_t index = 0; index < tiles->Size(); ++index)
    {
        const Core::Json& tile = tiles->At(index);
        read.push_back(tile.IsNumber() ? tile.Get<int>() : EmptyTile);
    }
    SetTiles(std::move(read));
}

}
