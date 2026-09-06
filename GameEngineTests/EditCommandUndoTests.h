#pragma once

/// <summary>
/// 편집 명령 열 종류의 적용·Undo·Redo가 각 단계의 상태를 복원하는지 확인한다.
/// 검사는 장면과 좁은 편집 호스트만 사용하므로 프로젝트 로드나 창이 필요하지 않다.
/// </summary>
[[nodiscard]] bool RunEditCommandUndoTests();
