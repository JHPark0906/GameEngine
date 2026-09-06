#pragma once

/// <summary>
/// <c>.gameproject</c>의 선택적 <c>icon</c> 필드(에셋 guid)가 왕복하는지 고정한다: 적어
/// 두면 다시 읽어도 같은 값이고, 비워 두면 다시 써도 줄이 생기지 않는다.
///
/// 아이콘 없음이 유효한 설정이라는 것이 두 번째 절반의 이유다 — 없던 프로젝트가 저장 한
/// 번으로 <c>"icon": ""</c> 같은 줄을 얻으면, 그 줄이 실제로 무언가를 가리키는지 다음 읽는
/// 사람이 다시 판단해야 한다.
/// </summary>
[[nodiscard]] bool RunProjectIconFieldTests();
