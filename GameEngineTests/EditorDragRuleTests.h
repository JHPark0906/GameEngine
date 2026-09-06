#pragma once

/// <summary>
/// 계층 재부모 드래그와 셸 패널 드래그의 상태 전이가 GameEditor::DragGesture와
/// 같은 답을 내는지 확인한다.
/// </summary>
[[nodiscard]] bool RunEditorDragRuleTests();

/// <summary>
/// 씬 뷰의 두 손짓이 <c>GameEditor::DragGesture</c>와 다른 규칙을 쓴다는 사실을 고정한다: 궤도 회전은
/// 누적 이동량으로 판정하며 첫 프레임부터 적용되고, 기즈모는 문턱 없이 즉시 잡는다.
///
/// 합치지 않기로 한 결정이 주석이 아니라 시험으로 서 있게 하는 것이 이 시험의 목적이다. 둘 중
/// 하나라도 <c>DragGesture</c>로 바꾸면 여기가 붉어진다.
/// </summary>
[[nodiscard]] bool RunSceneViewGestureTests();
