#pragma once

/// <summary>
/// CMakePresets.json의 빌드 트리들이 같은 산출물을 덮어쓰지 않는지 확인한다.
/// 어떤 두 프리셋도 같은 (출력 루트, 구성)을 사용할 수 없다. 구성이 고정되지 않은
/// 멀티 구성 프리셋은 그 출력 루트를 다른 프리셋과 공유하지 않아야 한다.
/// </summary>
[[nodiscard]] bool RunBuildOutputRootTests();
