#pragma once

/// <summary>
/// New Project, Open Project, New Script, New Scene, Rename Scene을 가짜 파일 대화상자로 검사한다.
/// 취소하면 아무 상태도 바뀌지 않아야 하며, 경로를 선택하면 지정한 파일과 편집기 상태가
/// 올바르게 변경되어야 한다. 실제 창을 띄울 필요가 없다.
/// </summary>
[[nodiscard]] bool RunEditorFileDialogTests();
