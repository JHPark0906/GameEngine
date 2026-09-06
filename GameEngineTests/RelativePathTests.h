#pragma once

/// <summary>
/// RelativePathWithin은 Scenes/../../ 같은 중첩 경로를 포함해 루트 밖의 대상을 거절해야 한다.
/// </summary>
[[nodiscard]] bool RunRelativePathTests();
