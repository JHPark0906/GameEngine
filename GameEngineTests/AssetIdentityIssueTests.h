#pragma once

/// <summary>
/// 편집기 없이 데이터베이스와 콘텐츠 소스만으로 정체성을 발급할 수 있는지 확인한다.
/// 이미 발급한 정체성은 참조가 끊기지 않도록 덮어쓰지 않아야 한다.
/// </summary>
[[nodiscard]] bool RunAssetIdentityIssueTests();
