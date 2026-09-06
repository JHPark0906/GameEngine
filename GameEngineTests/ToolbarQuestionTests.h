#pragma once

/// <summary>
/// 툴바가 사람에게 묻는 글들이다. 무엇을 지우는지 이름으로 적는가 같은 것은 창 없이 잴 수
/// 있어야 한다 — 무엇에 답하는지 모르는 물음은 답을 받아도 답이 아니고, 이 글들이 다루는
/// 것은 사람의 파일과 되돌릴 수 없는 동작이다.
/// </summary>
[[nodiscard]] bool RunToolbarQuestionTests();

/// <summary>
/// 툴바에서 확인 버튼을 제외하면 줄 수와 높이가 늘지 않고 남은 모든 버튼의 사각형이 보존되는지 확인한다.
/// </summary>
[[nodiscard]] bool RunToolbarFoldAfterQuestionsMovedTests();
