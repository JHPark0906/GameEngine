#pragma once

// editor-layer: 0 (Rules)

namespace GameEditor
{

/// <summary>이번 프레임에 잡기 상태를 정하는 데 필요한 사실들이다.</summary>
struct PlayInputFocusFrame
{
    /// <summary>플레이 중인지다. 편집 중에는 게임이 입력을 쥘 이유가 없다.</summary>
    bool isPlaying = false;
    /// <summary>에디터 창이 앞에 있는지다.</summary>
    bool windowHasFocus = true;
    /// <summary>이번 프레임에 게임 뷰 안에서 마우스 왼쪽이 눌렸는지다.</summary>
    bool pressedInsideGameView = false;
    /// <summary>이번 프레임에 Esc가 눌렸는지다.</summary>
    bool escapePressed = false;
};

/// <summary>잡기 상태가 이번 프레임에 어떻게 되는지다.</summary>
struct PlayInputFocusResult
{
    /// <summary>이 프레임이 끝난 뒤 게임이 입력을 쥐고 있는지다.</summary>
    bool captured = false;
    /// <summary>
    /// 이번 프레임의 클릭을 게임에 전하지 않고 삼키는지다. 잡는 클릭이 게임 안에서 발사가
    /// 되면 사람은 놀란다 — 포커스를 주려고 누른 것이 행동이 되어서는 안 된다.
    /// </summary>
    bool swallowClick = false;
};

/// <summary>
/// 게임이 입력을 쥐는지를 정한다. 게임 뷰를 클릭하면 쥐고, Esc로 놓는다 — 커서가 지나가는
/// 것만으로는 바뀌지 않는다. 커서 위치로 정하면 마우스가 스치는 것만으로 단축키가 게임으로
/// 새고, 사람은 자기가 무엇을 조작하는지 알 수 없게 된다.
///
/// 놓는 조건이 셋인 이유는 갇히지 않기 위해서다: Esc, 플레이 종료, 창이 뒤로 물러남. 잡힌
/// 채로 빠져나갈 길이 없는 상태를 만들지 않는다.
///
/// <b>Esc는 놓기가 이긴다.</b> 게임이 Esc를 쓰고 싶어 해도 이 프레임에서는 놓기가 먼저다.
/// 게임에게 Esc를 주는 길은 나중에 정할 문제이고 — 그때는 잡기를 푸는 다른 손짓이 필요하다 —
/// 지금은 빠져나갈 길이 하나라도 확실한 쪽을 고른다.
/// </summary>
/// <param name="frame">이번 프레임의 사실들이다.</param>
/// <param name="wasCaptured">이전 프레임이 끝났을 때 쥐고 있었는지다.</param>
/// <returns>이번 프레임의 결과다.</returns>
[[nodiscard]] PlayInputFocusResult ResolvePlayInputFocus(
    const PlayInputFocusFrame& frame, bool wasCaptured);

}
