#pragma once

[[nodiscard]] bool RunGameTimerTests();
[[nodiscard]] bool RunFrameLimiterTests();

/// <summary>창에 그려지는 런타임이 캡처 뷰 유무와 무관하게 자기 면 크기를 받는지 검증한다.</summary>
[[nodiscard]] bool RunWindowSurfaceSizeTests();

/// <summary>
/// 전체화면에서 돌아올 자리를 정하는 규칙을 검증한다. 모니터가 사라지거나 해상도가 줄어든 뒤
/// 기억한 자리로 돌려보내면 창이 보이지 않는 곳에 놓이므로, 그 판정이 이 시험의 대상이다.
/// </summary>
[[nodiscard]] bool RunWindowPlacementTests();

/// <summary>
/// 업데이트가 없앤 런타임을 그 프레임의 캡처 루프가 그리지 않는지 검증한다. 뷰 목록은 업데이트
/// 전에 모이고 그 뒤에 역참조되므로, 다시 받지 않으면 해제된 객체를 읽는다.
/// </summary>
[[nodiscard]] bool RunCaptureViewLifetimeTests();
