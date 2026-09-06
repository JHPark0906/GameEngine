#pragma once

// editor-layer: 2 (Views)

#include "UI/UIContext.h"

namespace GameEditor
{

class EditorTilemapTool;

/// <summary>
/// 타일 팔레트 패널이다: 페인팅 스위치, 붓/지우개, 그리고 타일셋의 칸들.
///
/// 팔레트가 자기 패널을 갖는 이유는 그것이 세로로 긴 목록이기 때문이다. 다른 패널이 자리를
/// 빌려 주면 자기 높이를 가질 수 없어, 타일 하나를 고르는 데 그 패널을 스크롤하고 그 안에서 또
/// 스크롤해야 한다. 패널이면 슬롯의 높이가 곧 팔레트의 높이이고, 무엇보다 사용자가 원하는
/// 자리로 끌어다 놓을 수 있다 — 패널을 재배치할 수 있게 만들어 둔 것이 이런 경우를 위해서다.
///
/// 이 클래스가 얇은 이유는 그리는 일도 칠하는 일도 도구(<see cref="EditorTilemapTool"/>)의
/// 것이기 때문이다. 패널은 다른 패널들과 같은 방식으로 슬롯에 놓이고 자기 자리를 도구에 넘긴다.
/// </summary>
class EditorTilePalettePanel final
{
public:
    /// <summary>UI와 도구만 받는다. 다른 패널과 달리 셸의 배율도 문맥도 쓰지 않는다 — 그리는
    /// 일이 전부 도구의 것이라, 쓰지 않는 참조를 들고 있으면 그것이 거짓말이 된다.</summary>
    EditorTilePalettePanel(GameEngine::UI::UIContext& ui, EditorTilemapTool& tilemapTool);

    /// <summary>패널의 내용 영역에 팔레트를 그린다.</summary>
    /// <param name="content">제목줄을 제외한 패널의 자리다.</param>
    void Draw(const GameEngine::UI::UIRect& content);

private:
    GameEngine::UI::UIContext& mUI;
    EditorTilemapTool& mTilemapTool;
};

}
