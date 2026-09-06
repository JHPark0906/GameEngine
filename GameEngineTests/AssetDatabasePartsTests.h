#pragma once

/// <summary>
/// 에셋 데이터베이스의 목록·매니페스트·페이로드 캐시가 독립적으로 동작하는지 본다.
/// 매니페스트를 쓰고 읽는 동안 페이로드가 로드되지 않아야 하며, 페이로드 캐시는
/// 매니페스트 없이 로드하고 퇴거할 수 있어야 한다.
///
/// 빌드는 페이로드 없이 매니페스트를 쓰고, 런타임은 매니페스트를 모르고 페이로드를 쓴다.
/// UnloadUnreferenced와 InheritPayloadsFrom의 호출자도 매니페스트에 의존하지 않는다.
/// </summary>
[[nodiscard]] bool RunAssetDatabasePartsTests();
