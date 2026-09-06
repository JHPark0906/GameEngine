#pragma once

/// <summary>
/// RenderFrame이 보관한 글리프 아틀라스 페이지가 발행 후 변하지 않는지 확인한다.
/// 게임 스레드에서 글리프를 더해도 렌더 스레드가 받은 이미지의 바이트는 유지되어야 한다.
/// 페이지가 자랄 때만 복사하고 예열이 끝난 정상 상태에서는 복사 횟수가 0인지 함께 측정한다.
/// </summary>
[[nodiscard]] bool RunTextPageImmutabilityTests();
