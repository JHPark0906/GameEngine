#pragma once

/// <summary>
/// 포인터를 잡은 요소가 커서 아래를 떠나도 같은 드래그가 이어져야 한다.
/// 창을 끌면 제목줄이 커서 밖으로 이동할 수 있으므로 눌림의 시각 상태와 포인터 캡처 수명을 구분한다.
/// </summary>
[[nodiscard]] bool RunUIPointerCaptureTests();
