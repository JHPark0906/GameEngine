#pragma once

/// <summary>
/// 정체성이 없는 에셋은 명시적 발급 플래그 없이 패키징할 수 없어야 한다.
/// 기존 정체성은 덮어쓰지 않으며 발급을 거절하면 파일을 남기지 않아야 한다.
/// </summary>
[[nodiscard]] bool RunPackagingIdentityFlagTests();
