#pragma once

/// <summary>
/// 자동 선택이 D3D12를 먼저 고르고, 만들 수 없을 때만 다음으로 내려가는지 확인한다.
/// </summary>
[[nodiscard]] bool RunAutomaticBackendChoiceTests();
