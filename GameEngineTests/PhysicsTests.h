#pragma once

/// <summary>
/// 축 정렬 사각형의 겹침 규칙이다. 컴포넌트도 장면도 없이 값만으로 선다 — 판정이 시스템 안에
/// 묻히지 않고 값 타입에 있는 덕이다.
/// </summary>
[[nodiscard]] bool RunAabb2DTests();

/// <summary>
/// 콜라이더가 서로 겹치는 것을 시스템이 판정해 들어옴·머묾·나감으로 나눠 주는지 고정한다.
/// 임시 장면에 오브젝트를 세워 프레임 단위로 확인하므로 화면이 필요 없다.
/// </summary>
[[nodiscard]] bool RunCollider2DTests();

/// <summary>
/// 타일맵 콜라이더가 채워진 칸에서만 겹치는지 확인한다. 격자 전체를 품는 사각형만 보면 빈 칸
/// 위에서도 겹쳤다고 답하므로, 좁은 판정이 실제로 도는지가 이 단위의 요점이다.
/// </summary>
[[nodiscard]] bool RunTilemapCollider2DTests();
