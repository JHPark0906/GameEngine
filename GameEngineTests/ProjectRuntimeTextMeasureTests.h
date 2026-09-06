#pragma once

/// <summary>
/// 프로젝트 폰트 등록 후 EditorContext의 텍스트 측정 캐시와 렌더링이 같은 폰트를 사용하는지 확인한다.
/// 시험 실행 파일 자체에는 폰트가 없으므로 스테이징된 편집기 콘텐츠를 사용한다.
/// 프로젝트 열기 동작 전체는 이 검사에 포함하지 않는다.
/// </summary>
[[nodiscard]] bool RunProjectRuntimeTextMeasureTests();
