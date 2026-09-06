#include "Views/EditorTilePalettePanel.h"

#include "Views/EditorTilemapTool.h"

namespace GameEditor
{

EditorTilePalettePanel::EditorTilePalettePanel(
    GameEngine::UI::UIContext& ui, EditorTilemapTool& tilemapTool)
    : mUI(ui), mTilemapTool(tilemapTool)
{
}

void EditorTilePalettePanel::Draw(const GameEngine::UI::UIRect& content)
{
    // 무엇을 그릴지도, 선택된 타일맵이 있는지도 도구가 안다. 패널은 자리만 넘긴다.
    mTilemapTool.DrawPalette(mUI, content);
}

}
