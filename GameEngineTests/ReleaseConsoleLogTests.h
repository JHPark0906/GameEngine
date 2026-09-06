#pragma once

/// <summary>
/// 명시적 로깅 활성화와 최근 로그 조회를 두 구성에서 확인한다. 구성별 기본 활성화 정책은 유지한다.
/// EditorBootstrap의 실제 GUI 실행이 로깅을 켜는지는 이 검사의 범위에 포함하지 않는다.
/// </summary>
[[nodiscard]] bool RunReleaseConsoleLogTests();
