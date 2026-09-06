#pragma once

/// <summary>
/// Play 중 입력을 에디터와 게임 중 누가 받는지 정하는 규칙이다. 게임 뷰를 클릭하면 게임이
/// 쥐고 Esc로 놓으며, 플레이가 끝나거나 창이 뒤로 물러나도 놓는다 — 잡힌 채 갇히는 상태가
/// 없어야 한다는 것이 이 규칙의 전부다.
/// </summary>
[[nodiscard]] bool RunPlayInputFocusTests();

/// <summary>
/// 잡은 입력이 실제로 두 런타임 사이에서 갈리는지 확인한다. 게임이 쥐면 게임의 Input이
/// 그 프레임을 받고 에디터의 Input은 비며, 쥐지 않으면 게임 쪽이 "아무것도 안 눌림"으로
/// 채워진다 — 채우지 않는 것과 다르다.
/// </summary>
[[nodiscard]] bool RunPlayInputHandoffTests();
