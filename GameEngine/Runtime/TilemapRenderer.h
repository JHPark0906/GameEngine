#pragma once

#include <vector>

#include "../Assets/AssetReference.h"
#include "../Core/Json.h"
#include "../Math/Color.h"
#include "../Math/Vector.h"
#include "Renderer.h"

namespace GameEngine::Runtime
{

/// <summary>
/// 타일 격자를 하나의 타일셋 시트로 그리는 컴포넌트다.
///
/// 격자의 칸마다 타일셋의 프레임 번호가 하나씩 놓이고, 비어 있는 칸은 <see cref="EmptyTile"/>
/// 이다. 타일셋을 나누는 방법은 스프라이트 시트와 같은 것을 쓴다 — 사이드카의 sheet 격자 —
/// 그래서 타일맵은 "어느 프레임을 어느 칸에" 만 말하고, 프레임이 이미지의 어디인지는 에셋이
/// 답한다.
///
/// 1차 범위는 레이어 하나와 타일셋 하나다. 장면 파일은 그래도 레이어를 목록으로 적고 로더는
/// 첫 레이어만 읽는데, 나중에 레이어가 여럿이 되어도 이미 저장된 장면이 깨지지 않게 하는
/// 최소한의 여지다. 여러 레이어를 위한 추상화는 필요해질 때 만든다.
///
/// 원점은 칸 (0, 0)의 왼쪽 아래 모서리이고, 열은 +x로 행은 +y로 자란다 — 씬 뷰에서 보는 방향
/// 그대로다.
/// </summary>
class TilemapRenderer final : public Renderer
{
public:
    /// <summary>빈 칸을 뜻하는 타일 번호다. 그리지 않는다.</summary>
    static constexpr int EmptyTile = -1;

    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>타일 그림을 담은 시트다. 사이드카의 sheet 격자가 프레임을 나눈다.</summary>
    [[nodiscard]] const Assets::AssetReference& GetTileset() const { return mTileset; }
    void SetTileset(Assets::AssetReference tileset);

    void CollectAssetReferences(std::vector<Assets::AssetReference>& references) const override
    {
        Renderer::CollectAssetReferences(references);
        if (mTileset.IsValid())
        {
            references.push_back(mTileset);
        }
    }

    /// <summary>격자의 열 수다. 1 이상이며, 바꾸면 타일 배열이 그 크기로 다시 잡힌다.</summary>
    [[nodiscard]] int GetColumns() const { return mColumns; }
    void SetColumns(int columns);

    /// <summary>격자의 행 수다. 1 이상이며, 바꾸면 타일 배열이 그 크기로 다시 잡힌다.</summary>
    [[nodiscard]] int GetRows() const { return mRows; }
    void SetRows(int rows);

    /// <summary>칸 하나의 크기다. 월드 단위이며 양수다.</summary>
    [[nodiscard]] const Math::Vector2& GetCellSize() const { return mCellSize; }
    void SetCellSize(const Math::Vector2& cellSize);

    /// <summary>타일 전체에 곱해지는 색이다.</summary>
    [[nodiscard]] const Math::Color& GetColor() const { return mColor; }
    void SetColor(const Math::Color& color) { mColor = color; }

    /// <summary>칸 하나의 타일 번호다. 격자 밖이면 <see cref="EmptyTile"/>이다.</summary>
    /// <param name="column">0부터 세는 열이다.</param>
    /// <param name="row">0부터 세는 행이다. 0이 맨 아래다.</param>
    [[nodiscard]] int GetTile(int column, int row) const;

    /// <summary>칸 하나의 타일을 바꾼다. 격자 밖이면 아무것도 하지 않는다.</summary>
    /// <returns>실제로 값이 바뀌었으면 true다. 페인팅이 이것으로 중복 기록을 피한다.</returns>
    bool SetTile(int column, int row, int tile);

    /// <summary>격자를 통째로 읽는다. 행 우선이며 크기는 columns * rows다.</summary>
    [[nodiscard]] const std::vector<int>& GetTiles() const { return mTiles; }

    /// <summary>
    /// 격자를 통째로 바꾼다. 크기가 columns * rows와 다르면 맞춰 자르거나 빈 칸으로 채운다.
    /// undo가 페인팅 이전 상태를 되돌릴 때 쓴다.
    /// </summary>
    void SetTiles(std::vector<int> tiles);

    /// <summary>타일 배열에서 칸 하나가 놓인 자리다. 격자 밖이면 -1이다.</summary>
    [[nodiscard]] int GetTileArrayIndex(int column, int row) const;

    /// <summary>칸 가운데의 로컬 좌표다. 격자 밖 좌표도 규칙대로 계산한다.</summary>
    [[nodiscard]] Math::Vector2 GetCellCenter(int column, int row) const;

    /// <summary>
    /// 로컬 좌표가 어느 칸인지 답한다. 격자 밖이면 false이며, 그때도 열과 행에는 계산된 값이
    /// 담긴다 — 페인팅이 격자를 벗어난 드래그를 조용히 흘려보낼 수 있게 한다.
    /// </summary>
    /// <param name="localX">타일맵 로컬 x다.</param>
    /// <param name="localY">타일맵 로컬 y다.</param>
    /// <param name="column">계산된 열을 받는다.</param>
    /// <param name="row">계산된 행을 받는다.</param>
    /// <returns>그 칸이 격자 안이면 true다.</returns>
    [[nodiscard]] bool TryGetCellAt(float localX, float localY, int& column, int& row) const;

private:
    void WriteExtraSerializedState(Core::Json::Object& members) const override;
    void ReadExtraSerializedState(const Core::Json& json) override;

    /// <summary>열·행이 바뀐 뒤 타일 배열을 새 크기로 다시 잡는다. 남는 칸은 비어 있다.</summary>
    void ResizeTiles();

    Assets::AssetReference mTileset;
    Math::Color mColor = Math::Color::White;
    Math::Vector2 mCellSize{ 1.0f, 1.0f };
    int mColumns = 8;
    int mRows = 8;
    std::vector<int> mTiles = std::vector<int>(64, EmptyTile);

    /// <summary>
    /// 읽었지만 그리지 않는 뒤의 레이어들이다. 이 판의 엔진은 첫 레이어만 그리므로, 나머지는
    /// 해석하지 않고 JSON 그대로 쥐고 있다가 저장 때 도로 쓴다 — 다층 장면을 이 판의 에디터로
    /// 열어 저장해도 잃지 않게 하는 유일한 이유다.
    /// </summary>
    Core::Json::Array mUnreadLayers;
};

}
