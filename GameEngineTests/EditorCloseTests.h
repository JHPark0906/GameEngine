#pragma once

/// <summary>
/// 저장되지 않은 편집이 있을 때 에디터를 닫아도 되는지 정하는 규칙이다. 창도 UI도 모르는
/// 순수 함수이므로 다섯 갈래를 창 없이 고정한다: 저장할 것이 없으면 묻지 않고 닫는다 ·
/// 그만두기면 창이 남는다 · 버리기면 저장하지 않고 닫는다 · 저장 성공이면 한 번 저장하고
/// 닫는다 · 저장 실패면 닫지 않는다.
/// </summary>
[[nodiscard]] bool RunEditorCloseDecisionTests();

/// <summary>
/// 저장이 성공을 보고했을 때 그 편집이 실제로 파일에 있는지 확인한다. 닫기 질문의 저장
/// 갈래는 이 답 하나로 닫을지를 정하므로, 참을 돌려주면서 쓰지 않으면 사람은 저장한 줄
/// 알고 잃는다.
/// </summary>
[[nodiscard]] bool RunSaveWritesTheFileTests();

/// <summary>쓸 수 없을 때 저장이 실패를 실패로 돌려주는지 확인한다.</summary>
[[nodiscard]] bool RunSaveFailureIsReportedTests();

/// <summary>
/// 종료 물음의 세 버튼이 서로 겹치지 않고 대화상자 안에 들어가는지, 그리고 긴 글이 잘리는
/// 대신 버튼을 넓히는지 확인한다. 사람이 무엇을 누르는지 알 수 없게 되는 것이 이 자리에서
/// 가장 나쁜 실패다.
/// </summary>
[[nodiscard]] bool RunCloseDialogLayoutTests();

/// <summary>
/// 물음의 글과 사본의 수명이 실제 동작과 같은지 확인한다. 사본이 남았을 때만 남는다고 말해야
/// 하고, 사본은 그것이 유일한 되돌릴 길일 때만 남아야 한다.
/// </summary>
[[nodiscard]] bool RunCloseQuestionAndCopyTests();
