#pragma once

/// <summary>
/// 파일을 읽는 동안 다시 쓸 때 독자가 온전한 내용만 받는지 확인한다.
/// 제자리 쓰기는 빈 파일이나 일부 내용이 노출될 수 있다.
/// 임시 파일을 쓴 뒤 이름을 바꾸는 교체에서는 이전 내용 전체 또는 새 내용 전체만 읽혀야 한다.
/// </summary>
[[nodiscard]] bool RunSharedFileWriteTests();
