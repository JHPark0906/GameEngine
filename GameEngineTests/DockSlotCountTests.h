#pragma once

/// <summary>
/// 도킹 슬롯의 개수가 컬럼 구성과 어긋나지 않는지 고정한다.
///
/// 이 자리가 필요한 이유는 개수가 두 곳에서 따로 정해질 수 있기 때문이다. 한쪽은
/// <c>DockSlotCount</c>라는 수이고 다른 한쪽은 <c>ComputeDockSlotRects</c>가 실제로
/// 만들어 놓는 사각형의 배치다. 수는 컬럼 셋의 합으로 적혀 있지만, 그 합이 진짜로
/// 기하와 같은지는 세어 보아야만 안다 — 왼쪽을 넷으로 쪼개 놓고 합만 그대로 두면
/// 배열 크기는 맞고 배치만 틀리며, 그런 어긋남은 화면에서야 드러난다.
/// </summary>
[[nodiscard]] bool RunDockSlotCountTests();
