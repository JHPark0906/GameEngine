#pragma once

/// <summary>
/// 프로젝트를 열면 저장되지 않은 편집, 선택 오브젝트, Undo 이력과 Play 표시를 모두 비워야 한다.
/// 다음 부팅에서 열 장면의 기록과 리비전 증가는 장면을 성공적으로 올린 뒤에만 수행해야 한다.
/// 실패한 장면을 시작 장면으로 기록하거나 바뀌지 않은 장면의 리비전을 올리면 안 된다.
/// </summary>
[[nodiscard]] bool RunSceneDocumentCloseTests();
