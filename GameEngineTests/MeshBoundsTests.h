#pragma once

/// <summary>
/// 메시 경계가 점들을 포함하는 최소 상자인지 확인한다.
/// 폭이 0인 경계도 유효하며 원점을 포함하지 않는 메시의 경계에 원점을 임의로 더하지 않는다.
/// </summary>
[[nodiscard]] bool RunMeshBoundsTests();
