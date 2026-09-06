#pragma once

/// <summary>
/// 메뉴가 화면 위에서 열리고 닫히고 골라지는 것을 고정한다.
///
/// 여는·닫는 <b>규칙</b>은 이미 <c>EditorMenuBarState</c>가 창 없이 지키고 있다. 여기서 묻는
/// 것은 그 규칙이 자리와 만났을 때다 — 무엇이 「밖」인가, 구분선과 흐린 줄을 누르면 어떻게
/// 되는가, 열린 목록이 정말 그 머리줄 아래에 서는가. 자리를 아는 곳이 뷰뿐이므로 이 답도
/// 뷰의 것이며, 진짜 트리를 세워 재야 답이 나온다.
/// </summary>
[[nodiscard]] bool RunEditorMenuBarInteractionTests();
