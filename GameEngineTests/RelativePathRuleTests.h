#pragma once

/// <summary>
/// 루트 기준 상대 경로 계산이 공용 헬퍼 밖에 없는지 확인한다.
/// lexically_relative 호출을 검사하여 같은 경로 규칙이 호출자마다 달라지지 않게 한다.
/// lexically_normal은 루트와 무관하게 경로 하나를 다듬는 데도 쓰이므로 검사 대상에서 제외한다.
/// </summary>
[[nodiscard]] bool RunRelativePathRuleTests();
