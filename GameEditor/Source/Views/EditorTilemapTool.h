#pragma once

// editor-layer: 2 (Views)

#include <cstddef>
#include <vector>

#include "Rules/EditorSceneTool.h"
#include "Core/UndoStack.h"
#include "Math/Vector.h"
#include "UI/UIContext.h"

namespace GameEngine::Runtime
{
class TilemapRenderer;
}

namespace GameEditor
{

class EditorContext;


/// <summary>
/// 타일 페인팅 한 번 — 붓을 누르고 끌다 뗀 한 획 — 을 되돌리는 커맨드다.
///
/// 획이 지나간 칸들만 (이전 값, 새 값)으로 기록한다. 격자 전체를 두 벌 뜨는 대신 바뀐 칸만
/// 담으므로, 큰 타일맵에 한 칸을 칠한 undo가 격자만 한 메모리를 쓰지 않는다. 한 획이 커맨드
/// 하나라서 Ctrl+Z 한 번이 그 획 전체를 지운다.
/// </summary>
class TilePaintCommand final : public GameEngine::Core::IEditCommand
{
public:
    /// <summary>획이 바꾼 칸 하나다. 배열 자리와 그 자리의 이전·새 타일 번호다.</summary>
    struct Change
    {
        int arrayIndex = 0;
        int before = 0;
        int after = 0;
    };

    TilePaintCommand(EditorContext& context, unsigned int componentId, std::vector<Change> changes);

    [[nodiscard]] bool Apply() override;
    [[nodiscard]] bool Revert() override;

private:
    [[nodiscard]] bool Write(bool useAfter) const;

    EditorContext* mContext;
    unsigned int mComponentId;
    std::vector<Change> mChanges;
};

/// <summary>
/// 타일맵을 칠하는 에디터 도구다: 팔레트에서 타일을 고르고, 씬 뷰에서 격자에 맞춰 칠하거나
/// 지운다.
///
/// 선택한 타일과 획의 상태는 도구가 소유한다. 팔레트 패널은 그릴 자리를,
/// 씬 뷰는 칠하기 입력을 이 도구에 넘긴다.
/// </summary>
class EditorTilemapTool final : public ISceneTool
{
public:
    EditorTilemapTool(EditorContext& context);

    /// <summary>지금 칠할 수 있는 상태인지다. 선택된 객체에 타일맵이 있고 도구가 켜져 있을 때다.</summary>
    [[nodiscard]] bool IsPainting() const { return mPaintingEnabled; }

    /// <summary>
    /// 팔레트 패널의 자리를 통째로 그린다: 페인팅 켜기, 붓/지우개, 그리고 타일셋의 프레임들.
    ///
    /// 선택된 객체에 타일맵이 없으면 안내 문구만 남긴다.
    /// 패널은 언제나 자리에 있으므로 비어 있는 이유를 알려 준다.
    ///
    /// 고른 타일과 붓/지우개, 페인팅 스위치는 이 도구의 상태라 선택이 바뀌어도 남는다. 다른
    /// 객체를 잠깐 확인하고 돌아오는 것은 타일맵을 그리는 사람이 끊임없이 하는 일이라, 돌아올
    /// 때마다 고르던 타일을 다시 찾아야 한다면 그것이 새 마찰이 된다.
    /// </summary>
    /// <param name="ui">위젯을 그릴 UI 컨텍스트다.</param>
    /// <param name="content">팔레트 패널의 내용 영역이다.</param>
    void DrawPalette(GameEngine::UI::UIContext& ui, const GameEngine::UI::UIRect& content);

    /// <summary>
    /// 씬 뷰의 왼쪽 드래그를 붓질로 처리한다. 도구가 꺼져 있거나 선택된 타일맵이 없으면 아무것도
    /// 가져가지 않아, 씬 뷰가 평소의 궤도 회전을 계속하게 한다.
    ///
    /// 붓이 쓰는 것은 왼쪽 버튼뿐이므로 가져가는 것도 그것뿐이다. 가운데 드래그의 팬과 휠의
    /// 돌리는 페인팅 중에도 씬 뷰의 것으로 남는다 — 칠하면서 시점을 옮기는 것은 타일맵을
    /// 그리는 사람이 끊임없이 하는 일이다.
    /// </summary>
    /// <param name="input">이번 프레임의 커서 광선과 버튼 상태다.</param>
    /// <returns>붓질 중이면 왼쪽 버튼을, 아니면 아무것도 가져가지 않는다.</returns>
    [[nodiscard]] SceneInputCapture HandleSceneInput(const SceneToolInput& input) override;

private:
    /// <summary>선택된 객체의 타일맵이다. 없으면 null이다.</summary>
    [[nodiscard]] GameEngine::Runtime::TilemapRenderer* FindSelectedTilemap() const;

    /// <summary>한 획을 마치고 undo 스택에 올린다. 바꾼 칸이 없으면 아무것도 올리지 않는다.</summary>
    void FinishStroke();

    EditorContext& mContext;

    /// <summary>씬 뷰의 왼쪽 드래그가 붓질인지 궤도 회전인지를 가르는 스위치다.</summary>
    bool mPaintingEnabled = false;
    /// <summary>지우개면 참이다. 붓이면 고른 타일을 칠한다.</summary>
    bool mErasing = false;
    /// <summary>팔레트에서 고른 타일 번호다.</summary>
    int mSelectedTile = 0;

    /// <summary>진행 중인 획이 지금까지 바꾼 칸들이다. 버튼을 떼면 커맨드가 된다.</summary>
    std::vector<TilePaintCommand::Change> mStroke;
    /// <summary>획이 칠하고 있는 타일맵의 컴포넌트 id다. 획이 없으면 0이다.</summary>
    unsigned int mStrokeComponentId = 0;
};

}
