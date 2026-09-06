#pragma once

/// <summary>
/// 툴바가 선언한 버튼들이 배치에 빠짐없이 실리는지 확인한다.
/// 소스에서 버튼을 발견해야 새로 추가한 버튼도 검사 대상이 된다. 손으로 옮겨 적은 목록은
/// 실제 툴바에 있는 버튼을 누락하고도 검사를 통과할 수 있다.
/// </summary>
[[nodiscard]] bool RunEditorToolbarDeclarationTests();
