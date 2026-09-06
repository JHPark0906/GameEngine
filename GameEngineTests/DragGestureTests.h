#pragma once

/// <summary>
/// 누르고 끌고 놓는 손짓의 규칙이다. 위젯도 화면도 없이 좌표만으로 서므로, 문턱을 넘기 전과 뒤,
/// 그리고 뗌이 클릭인지 놓기인지가 여기서 고정된다.
/// </summary>
[[nodiscard]] bool RunDragGestureTests();
