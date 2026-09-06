#pragma once

[[nodiscard]] bool RunContentSourceTests();
[[nodiscard]] bool RunContentPackTests();

/// <summary>
/// 디렉터리 감시가 파일 변동을 알리고, 조용할 때는 침묵하며, 하위 디렉터리까지 지켜보는지를
/// 실제 임시 디렉터리로 확인한다.
/// </summary>
[[nodiscard]] bool RunDirectoryWatcherTests();
