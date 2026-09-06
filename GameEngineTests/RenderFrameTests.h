#pragma once

[[nodiscard]] bool RunRenderFrameTests();
[[nodiscard]] bool RunSceneCameraFallbackTests();

/// <summary>
/// RenderFrame이 나르는 어떤 타입도 생 포인터를 쥐지 않는지 확인한다. 프레임이 렌더 스레드로
/// 건너간 뒤에도 게임 스레드는 장면을 계속 바꾸므로, 이 성질이 깨지면 렌더 스레드가 사라진
/// 것을 읽는다.
/// </summary>
[[nodiscard]] bool RunRenderFramePointerContractTests();

/// <summary>화면 공간 텍스트가 자기 RectTransform의 좌상단에 실리는지 확인한다.</summary>
[[nodiscard]] bool RunScreenSpaceTextPlacementTests();

/// <summary>
/// 툴바가 선언한 사각형들을 배율 2.0에서 창 없이 세워, 라벨이 자기 버튼 안에 놓이는지 확인한다.
/// 화면의 픽셀로는 버튼 경계를 읽을 수 없어서 — 채움색이 배경색과 같다 — 계산 쪽을 직접 묻는다.
/// </summary>
[[nodiscard]] bool RunToolbarLabelCentringTests();
[[nodiscard]] bool RunQuadDrawGeometryTests();
[[nodiscard]] bool RunSlicedSpriteQuadTests();

/// <summary>
/// 화면 공간 9-슬라이스의 모서리가 화면 배율을 타는지다.
/// </summary>
[[nodiscard]] bool RunSlicedBorderScaleTests();
[[nodiscard]] bool RunTilemapDrawTests();
[[nodiscard]] bool RunClipSpaceTests();

/// <summary>타일맵 한 레이어가 제출 층에서 몇 번의 draw가 되는지 센다.</summary>
[[nodiscard]] bool RunTilemapSubmissionTests();
