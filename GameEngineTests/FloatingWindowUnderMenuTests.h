#pragma once

/// <summary>
/// 메뉴 막대가 있는 화면에서도 떠 있는 창의 제목줄이 포인터를 잡을 수 있는지 확인한다.
/// 메뉴 캔버스는 펼친 목록이 가린 패널보다 위에 그려져야 한다.
/// 닫힌 메뉴는 입력 영역을 차지하지 않고 열린 목록은 자신이 덮은 영역에서만 포인터를 가져가야 한다.
/// </summary>
[[nodiscard]] bool RunFloatingWindowUnderMenuTests();
