#pragma once

/// <summary>
/// 되살리기 물음이 떠 있는 동안 사람이 창을 닫으려 하면 그 뜻은 "지금은 답하지 않겠다"이고,
/// 그것은 「나중에 정하기」와 같은 답이어야 한다 — 사본은 남고, 되살리지도 지우지도 않는다.
/// 물음이 밖에서 닫히는 길이 그 하나뿐이 아니므로(Esc, 닫기 상자), 규칙 쪽에서 한 번에
/// 고정한다.
/// </summary>
[[nodiscard]] bool RunRecoveryPromptCloseTests();

/// <summary>
/// 편집 모드의 프레임이 문서를 바꾸지 않는지 확인한다. 되살리기 물음이 첫 프레임 뒤에 서는
/// 것이 안전한 근거가 이것이다 — 사람을 기다리는 동안 장면이 그 순간 그대로여야, 되살릴지
/// 정하는 사이에 문서가 움직이지 않는다.
/// </summary>
[[nodiscard]] bool RunEditModeFrameLeavesDocumentTests();
