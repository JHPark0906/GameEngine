#pragma once

/// <summary>
/// 화면 공간 텍스트가 지정한 자리에서 시작하고 게임 뷰 안에 놓이는지 확인한다.
/// 프레임에 실린 글리프 사각형을 재면 창 없이 위치를 확인할 수 있다. 사각형이 뷰 밖에
/// 놓이면 픽셀을 올바르게 그려도 텍스트가 잘린다.
/// </summary>
[[nodiscard]] bool RunGameViewTextClipTests();
