#pragma once

/// <summary>
/// 사고로 닫힌 에디터가 남긴 장면 사본을 다시 찾아내는 규칙이다. 쓰는 쪽과 읽는 쪽이 같은
/// 경로 규칙을 보는지, 남의 파일을 건드리지 않는지, 그리고 주인이 사라진 사본을 골라낼 수
/// 있는지를 창 없이 고정한다.
/// </summary>
[[nodiscard]] bool RunEditorRecoveryRuleTests();

/// <summary>
/// 사본을 되살리면 그 편집이 장면에 돌아오고, 저장되지 않은 상태로 서는지 확인한다.
/// 디스크의 장면 파일은 사고 이전 그대로이므로, 저장할 것이 없다고 말하면 사람은 되살린
/// 작업을 두 번째로 잃는다.
/// </summary>
[[nodiscard]] bool RunEditorRecoveryRestoreTests();

/// <summary>
/// 사본을 두고 고른 답이 그대로 일어나는지 확인한다. 되살리기는 파일을 남기고 버리기만
/// 지우며, 되살리기가 실패해도 파일은 남는다 — 실패한 뒤 파일까지 없으면 두 번 잃는다.
/// </summary>
[[nodiscard]] bool RunEditorRecoveryChoiceTests();
