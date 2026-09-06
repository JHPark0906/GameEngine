#pragma once

/// <summary>
/// 편집기 소스가 자기보다 위 계층의 헤더를 include하지 않는지 확인한다.
/// 각 파일은 editor-layer 주석으로 계층 번호와 이름을 선언해야 하며 둘이 일치해야 한다.
/// 선언 누락·중복과 선언에 맞지 않는 include 관계도 실패로 처리한다.
/// </summary>
[[nodiscard]] bool RunEditorLayerTests();
