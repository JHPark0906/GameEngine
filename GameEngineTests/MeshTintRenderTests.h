#pragma once

/// <summary>
/// Mesh와 SkinnedMesh 파이프라인의 tint가 D3D11과 D3D12의 실제 픽셀에 반영되는지 확인한다.
/// 같은 장면을 흰 tint와 유색 tint로 각각 그린다. 흰 tint의 캡처 픽셀과 tint를 sRGB에서
/// 선형 공간으로 변환해 곱하고 다시 sRGB로 인코딩한 값을 유색 tint의 캡처와 비교한다.
/// 이 관계는 셰이더의 albedo * tint * lighting에 따른다.
/// </summary>
[[nodiscard]] bool RunMeshTintRenderTests();
