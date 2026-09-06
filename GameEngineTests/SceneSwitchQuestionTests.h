#pragma once

/// <summary>
/// 저장되지 않은 편집을 두고 다른 장면을 열지 묻는 글이다. 시도가 어떻게 끝났는지에 따라
/// 글이 갈리는지를 창 없이 잰다 — 「저장이 실패했다」와 「저장은 됐는데 못 열었다」는 사람이
/// 다음에 할 일이 다르고, 구별되지 않으면 사람은 저장을 두 번 시도한다.
/// </summary>
[[nodiscard]] bool RunSceneSwitchQuestionTests();
