#pragma once

/// <summary>
/// 주입한 시각으로 파일 감시의 quiet period를 확인한다.
/// 대기 시간이 끝나기 전에는 스캔하지 않고 끝나면 한 번 스캔하며 추가 알림은 대기를 다시 시작한다.
/// </summary>
[[nodiscard]] bool RunProjectWatchQuietPeriodTests();
