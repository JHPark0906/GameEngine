#pragma once

/// <summary>
/// 구 형식 .gameproject의 assetRootPath는 값과 무관하게 무시되며 프로젝트 열기를 막지 않는다.
/// 기존 파일을 수정하지 않아도 열려야 하고, 사용자가 무시되는 설정을 알 수 있도록 경고를
/// 한 번 남겨야 한다.
/// </summary>
[[nodiscard]] bool RunLegacyAssetRootPathTests();
