#pragma once

/// <summary>
/// 한 프로젝트에서 세운 정돈 계획을 다른 프로젝트에는 적용할 수 없는지 확인한다.
/// 계획은 조사한 프로젝트의 정체성을 보관하고 적용 대상이 다르면 거절해야 한다.
/// 호출자가 프로젝트 전환 시 계획을 지우는지에 의존하지 않는다.
/// </summary>
[[nodiscard]] bool RunAssetMaintenanceIdentityTests();
