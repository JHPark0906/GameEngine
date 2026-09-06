#pragma once

[[nodiscard]] bool RunMathTests();
[[nodiscard]] bool RunMatrixTests();

/// <summary>뷰포트 투영, 축 드래그 변환, 격자 스냅의 규칙을 검증한다.</summary>
[[nodiscard]] bool RunViewportProjectionTests();
