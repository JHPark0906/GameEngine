#pragma once

/// <summary>
/// 떠 있는 창의 몸통, panel-32 제목줄과 Dock 라벨이 올바른 그리기 목록으로 구성되는지 확인한다.
/// </summary>
[[nodiscard]] bool RunEditorFloatingPanelChromeTests();

/// <summary>
/// 컴포넌트의 존재뿐 아니라 떠 있는 창의 껍데기가 실제 프레임의 그리기 목록에 포함되는지 확인한다.
/// </summary>
[[nodiscard]] bool RunEditorFloatingPanelDrawTests();

/// <summary>
/// 셸이 제목줄의 눌림을 실제로 받는지 확인한다. 화면에서 창이 한 픽셀도 움직이지 않은 것이
/// 이 신호가 오지 않았을 때의 모습이고, 그 위층의 계약 시험은 이 자리를 못 잡는다.
/// </summary>
[[nodiscard]] bool RunEditorFloatingPanelPressTests();

/// <summary>
/// 창을 끄는 동안 하나의 드래그가 유지되는지 확인한다.
/// 창이 이동하면 제목줄이 커서 밖으로 나갈 수 있으므로 눌린 시각 상태로 드래그 수명을 정하면 안 된다.
/// </summary>
[[nodiscard]] bool RunEditorFloatingPanelDragTests();
