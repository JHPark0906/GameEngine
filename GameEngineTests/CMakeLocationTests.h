#pragma once

/// <summary>
/// 빌드 도구가 어느 <c>cmake</c>로 빌드할지 고르는 순서다.
///
/// 이것이 시험할 값이 있는 이유는 틀린 답이 조용하지 않고 요란하되 엉뚱하기 때문이다: 이 트리를
/// 만들지 않은 cmake는 트리의 제너레이터를 모를 수 있고, 그때 실패는 프로젝트를 한 줄도 읽기
/// 전에 난다.
/// </summary>
[[nodiscard]] bool RunCMakeLocationTests();
