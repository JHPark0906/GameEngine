#pragma once

/// <summary>
/// 실제 에셋과 렌더러·Collect 경로를 통해 Material 틴트가 픽셀에 반영되는지 확인한다.
/// 흰색과 유색 재질은 선형 색과 sRGB 변환을 고려해 비교한다.
/// Material이 아닌 참조는 충돌하거나 잘못 그려지지 않아야 한다.
/// </summary>
[[nodiscard]] bool RunMaterialRenderTests();
