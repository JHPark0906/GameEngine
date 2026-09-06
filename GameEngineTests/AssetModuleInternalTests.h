#pragma once

/// <summary>
/// AssetDatabaseInternal.h가 에셋 모듈 밖으로 노출되지 않는지 확인한다.
/// 내용 해시와 매니페스트 구현은 모듈 내부에서만 공유해 외부 코드와 결합하지 않는다.
/// </summary>
[[nodiscard]] bool RunAssetModuleInternalTests();
