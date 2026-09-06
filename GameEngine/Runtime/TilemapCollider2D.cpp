#include "pch.h"
#include "TilemapCollider2D.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>

#include "GameObject.h"
#include "PropertyDescriptor.h"
#include "TilemapRenderer.h"
#include "Transform.h"

namespace GameEngine::Runtime
{

namespace
{
    struct CellRange
    {
        int firstColumn = 0;
        int lastColumn = -1;
        int firstRow = 0;
        int lastRow = -1;
    };

    // 정수 변환 전에 격자로 제한한다. 큰 좌표를 먼저 int로 바꾸면 범위를 벗어날 수 있다.
    [[nodiscard]] CellRange FindOverlappingCells(
        const Math::Aabb2D& local, const TilemapRenderer& tilemap)
    {
        const Math::Vector2& cell = tilemap.GetCellSize();
        if (local.IsEmpty() || !std::isfinite(local.min.GetX()) || !std::isfinite(local.min.GetY()) ||
            !std::isfinite(local.max.GetX()) || !std::isfinite(local.max.GetY()) ||
            cell.GetX() <= 0.0f || cell.GetY() <= 0.0f)
        {
            return {};
        }
        return {
            static_cast<int>(std::clamp(std::floor(local.min.GetX() / cell.GetX()),
                0.0f, static_cast<float>(tilemap.GetColumns()))),
            static_cast<int>(std::clamp(std::ceil(local.max.GetX() / cell.GetX()) - 1.0f,
                -1.0f, static_cast<float>(tilemap.GetColumns() - 1))),
            static_cast<int>(std::clamp(std::floor(local.min.GetY() / cell.GetY()),
                0.0f, static_cast<float>(tilemap.GetRows()))),
            static_cast<int>(std::clamp(std::ceil(local.max.GetY() / cell.GetY()) - 1.0f,
                -1.0f, static_cast<float>(tilemap.GetRows() - 1))) };
    }

    /// <summary>
    /// 모양은 렌더러가 정의하고, 고체 면의 정책만 콜라이더의 속성으로 보존한다.
    /// </summary>
    std::span<const PropertyDescriptor> TilemapCollider2DProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<TilemapCollider2D>(
                "oneWay", "One Way", &TilemapCollider2D::IsOneWay, &TilemapCollider2D::SetOneWay),
        };
        return properties;
    }

    [[nodiscard]] std::optional<Math::Aabb2DSweepHit> SweepTop(
        const Math::Aabb2D& movingBox, const Math::Aabb2D& tile,
        const Math::Vector2& displacement)
    {
        // 착지 위치를 적분하고 콜라이더의 발바닥을 다시 계산하면 각각 반올림이 생긴다.
        // 고정 epsilon은 높은 좌표의 float 간격보다 작아질 수 있으므로, 그 좌표 크기의 두 ULP만
        // 허용한다. 이미 아래에 있는 몸체를 넓은 두께의 가상 발판으로 붙잡지는 않는다.
        const float coordinateScale = (std::max)({ 1.0f, std::abs(movingBox.min.GetY()),
            std::abs(movingBox.max.GetY()), std::abs(tile.max.GetY()) });
        const float contactEpsilon = 2.0f *
            (std::nextafter(coordinateScale, std::numeric_limits<float>::infinity()) - coordinateScale);
        const float distance = movingBox.min.GetY() - tile.max.GetY();
        if (tile.IsEmpty() || displacement.GetY() >= 0.0f || distance < -contactEpsilon)
        {
            return std::nullopt;
        }
        const float fraction = (std::max)(0.0f, distance / -displacement.GetY());
        if (fraction > 1.0f)
        {
            return std::nullopt;
        }
        // 발바닥이 윗면 높이를 지나는 같은 시점의 X 범위를 본다. 끝 위치만 보면 대각선 착지를 놓친다.
        const float minX = movingBox.min.GetX() + displacement.GetX() * fraction;
        const float maxX = movingBox.max.GetX() + displacement.GetX() * fraction;
        if (maxX <= tile.min.GetX() || minX >= tile.max.GetX())
        {
            return std::nullopt;
        }
        return Math::Aabb2DSweepHit{ fraction, { 0.0f, 1.0f } };
    }
}

const ComponentType& TilemapCollider2D::StaticType()
{
    static const ComponentType type{
        "TilemapCollider2D", &Collider2D::StaticType(), &TilemapCollider2DProperties,
        &MakeComponentInstance<TilemapCollider2D> };
    return type;
}

const TilemapRenderer* TilemapCollider2D::FindTilemap() const
{
    const GameObject* const owner = GetGameObject();
    return owner ? owner->GetComponent<TilemapRenderer>() : nullptr;
}

Math::Aabb2D TilemapCollider2D::GetWorldBounds() const
{
    const TilemapRenderer* const tilemap = FindTilemap();
    if (!tilemap)
    {
        return Math::Aabb2D{};
    }
    const Math::Vector2& cell = tilemap->GetCellSize();
    const Math::Aabb2D local{
        { 0.0f, 0.0f },
        { cell.GetX() * static_cast<float>(tilemap->GetColumns()),
          cell.GetY() * static_cast<float>(tilemap->GetRows()) }
    };
    return TransformToWorld(local);
}

bool TilemapCollider2D::OverlapsBox(const Math::Aabb2D& box) const
{
    const TilemapRenderer* const tilemap = FindTilemap();
    if (!tilemap)
    {
        return false;
    }
    const Math::Vector2& cell = tilemap->GetCellSize();
    if (cell.GetX() <= 0.0f || cell.GetY() <= 0.0f)
    {
        return false;
    }

    // 상대 사각형이 덮는 칸의 범위만 본다. 격자가 아무리 넓어도 검사는 그 사각형이 걸친 칸 수에
    // 비례한다.
    const Math::Aabb2D local = TransformToLocal(box);
    if (local.IsEmpty())
    {
        return false;
    }
    const CellRange range = FindOverlappingCells(local, *tilemap);
    for (int row = range.firstRow; row <= range.lastRow; ++row)
    {
        for (int column = range.firstColumn; column <= range.lastColumn; ++column)
        {
            if (tilemap->GetTile(column, row) == TilemapRenderer::EmptyTile)
            {
                continue;
            }
            const Math::Aabb2D cellBox{
                { static_cast<float>(column) * cell.GetX(),
                  static_cast<float>(row) * cell.GetY() },
                { static_cast<float>(column + 1) * cell.GetX(),
                  static_cast<float>(row + 1) * cell.GetY() }
            };
            if (cellBox.Overlaps(local))
            {
                return true;
            }
        }
    }
    return false;
}

bool TilemapCollider2D::OverlapsCollider(const Collider2D& other) const
{
    const TilemapRenderer* const tilemap = FindTilemap();
    if (!tilemap) return false;
    const Math::Aabb2D otherBounds = other.GetWorldBounds();
    const Math::Vector2& cell = tilemap->GetCellSize();
    const CellRange range = FindOverlappingCells(TransformToLocal(otherBounds), *tilemap);
    for (int row = range.firstRow; row <= range.lastRow; ++row)
    {
        for (int column = range.firstColumn; column <= range.lastColumn; ++column)
        {
            if (tilemap->GetTile(column, row) == TilemapRenderer::EmptyTile) continue;
            const Math::Aabb2D worldCell = TransformToWorld({
                { static_cast<float>(column) * cell.GetX(), static_cast<float>(row) * cell.GetY() },
                { static_cast<float>(column + 1) * cell.GetX(), static_cast<float>(row + 1) * cell.GetY() } });
            if (worldCell.Overlaps(otherBounds) && other.OverlapsBox(worldCell)) return true;
        }
    }
    return false;
}

std::optional<Math::Aabb2DSweepHit> TilemapCollider2D::SweepBox(
    const Math::Aabb2D& movingBox, const Math::Vector2& worldDisplacement) const
{
    const TilemapRenderer* const tilemap = FindTilemap();
    if (!tilemap || movingBox.IsEmpty() || !std::isfinite(worldDisplacement.GetX()) ||
        !std::isfinite(worldDisplacement.GetY()) || (mOneWay && worldDisplacement.GetY() >= 0.0f))
    {
        return std::nullopt;
    }
    const Math::Vector2& cell = tilemap->GetCellSize();
    if (cell.GetX() <= 0.0f || cell.GetY() <= 0.0f)
    {
        return std::nullopt;
    }

    // 시작과 끝을 모두 품는 영역을 로컬 격자로 되돌린다. 양 끝에 한 칸을 더 보는 것은 닿기만
    // 한 상태에서 안쪽으로 들어갈 때도 그 칸을 후보로 남기기 위해서다. OverlapsBox와 달리
    // sweep은 바로 그 경계의 hit(0)를 알아야 한다.
    const Math::Aabb2D end{
        movingBox.min + worldDisplacement, movingBox.max + worldDisplacement };
    const Math::Aabb2D local = TransformToLocal(movingBox.UnitedWith(end));
    if (local.IsEmpty() || !std::isfinite(local.min.GetX()) || !std::isfinite(local.min.GetY()) ||
        !std::isfinite(local.max.GetX()) || !std::isfinite(local.max.GetY()))
    {
        return std::nullopt;
    }
    // 큰 낙하량도 정수 변환 전에 격자 범위로 제한한다. 격자 밖이면 시작 > 끝이 되어 비게 된다.
    const int firstColumn = static_cast<int>(std::clamp(
        std::floor(local.min.GetX() / cell.GetX()) - 1.0f,
        0.0f, static_cast<float>(tilemap->GetColumns())));
    const int firstRow = static_cast<int>(std::clamp(
        std::floor(local.min.GetY() / cell.GetY()) - 1.0f,
        0.0f, static_cast<float>(tilemap->GetRows())));
    const int lastColumn = static_cast<int>(std::clamp(
        std::ceil(local.max.GetX() / cell.GetX()),
        -1.0f, static_cast<float>(tilemap->GetColumns() - 1)));
    const int lastRow = static_cast<int>(std::clamp(
        std::ceil(local.max.GetY() / cell.GetY()),
        -1.0f, static_cast<float>(tilemap->GetRows() - 1)));

    const GameObject* const owner = GetGameObject();
    // 로컬 Y가 뒤집혀도 노출된 면은 월드 위쪽이어야 한다. 묻힌 셀의 경계는 착지면이 아니다.
    const int aboveRowStep = owner &&
        owner->GetTransform().GetLocalToWorldMatrix().TransformDirection({ 0.0f, 1.0f, 0.0f }).GetY() < 0.0f
        ? -1 : 1;

    std::optional<Math::Aabb2DSweepHit> earliest;
    for (int row = firstRow; row <= lastRow; ++row)
    {
        for (int column = firstColumn; column <= lastColumn; ++column)
        {
            if (tilemap->GetTile(column, row) == TilemapRenderer::EmptyTile)
            {
                continue;
            }
            if (mOneWay && tilemap->GetTile(column, row + aboveRowStep) != TilemapRenderer::EmptyTile)
            {
                continue;
            }
            const Math::Aabb2D localCell{
                { static_cast<float>(column) * cell.GetX(),
                  static_cast<float>(row) * cell.GetY() },
                { static_cast<float>(column + 1) * cell.GetX(),
                  static_cast<float>(row + 1) * cell.GetY() }
            };
            const Math::Aabb2D worldCell = TransformToWorld(localCell);
            const std::optional<Math::Aabb2DSweepHit> hit = mOneWay
                ? SweepTop(movingBox, worldCell, worldDisplacement)
                : movingBox.SweepAgainst(worldCell, worldDisplacement);
            if (hit && (!earliest || hit->fraction < earliest->fraction))
            {
                earliest = hit;
            }
        }
    }
    return earliest;
}

}
