#pragma once

/// <summary>
/// 편집기가 요청한 번들 폰트 파일을 찾아 등록하고 세 역할을 모두 배정하는지 확인한다.
/// 없는 파일을 요청하면 경고를 남기고 해당 역할만 비워야 한다.
/// 폰트 파서의 정확성은 별도 검사하며 여기서는 파일 탐색과 등록·배정 계약을 검증한다.
/// </summary>
[[nodiscard]] bool RunEditorFontLoadTests();
