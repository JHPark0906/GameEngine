#pragma once

/// <summary>
/// 저장이 씬 오브젝트의 id를 불필요하게 다시 매기지 않는지 확인한다.
/// 편집 없는 왕복은 파일 바이트를 보존하고 부모와 자식 연결을 유지해야 한다.
/// 새 오브젝트의 id는 파일에서 읽은 기존 id와 충돌하지 않아야 한다.
/// </summary>
[[nodiscard]] bool RunSceneObjectIdPreservationTests();
