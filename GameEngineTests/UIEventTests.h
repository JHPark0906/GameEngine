#pragma once

[[nodiscard]] bool RunUIPointerRouterTests();
[[nodiscard]] bool RunUIEventSystemTests();

[[nodiscard]] bool RunScrollRectTests();

/// <summary>버튼의 세 상태가 사람이 알아챌 만큼 다른 색으로 보이는지 검증한다.</summary>
bool RunButtonStateContrastTests();

/// <summary>
/// 한 프레임 안에서 시작하고 끝난 클릭이 버튼에 닿는지 확인한다. 상태를 표본으로만 읽는
/// 입력은 표본 사이에 끝난 누름·뗌을 보지 못한다.
/// </summary>
[[nodiscard]] bool RunSameFrameClickTests();

/// <summary>한 프레임 안에서 눌렸다 떼어진 키가 단축키에 닿는지 확인한다.</summary>
[[nodiscard]] bool RunSameFrameKeyTests();

/// <summary>
/// 유지 모드 UI가 포인터를 "가져갔다"고 답하는 자리와 답하지 않는 자리를 고정한다. 두 방향으로
/// 틀릴 수 있어 양쪽을 함께 잰다 — 안 가져가면 클릭이 아래로 새고, 지나치게 가져가면 즉시 모드
/// UI 전체가 눌리지 않는다.
/// </summary>
bool RunPointerConsumptionTests();

/// <summary>
/// 상태를 매 프레임 맞추는 호출자가 자기 클릭을 잃지 않는지 확인한다. 켜진 채로 다시 켜는 것은
/// 아무 일도 아니어야 하고, 끄는 것만이 상태를 지운다.
/// </summary>
bool RunButtonInteractableClickTests();

/// <summary>버튼의 상태 색이 그림 없이도 프레임까지 도달하는지 검증한다.</summary>
bool RunButtonSurfaceTests();

/// <summary>
/// 목록 요소의 자리와 값이다. 머리 칸과 펼쳐진 항목 줄이 어디를 덮는지, 고른 값이 글자로 어떻게
/// 보이는지를 화면 없이 고정한다.
/// </summary>
[[nodiscard]] bool RunDropdownTests();

/// <summary>
/// 커서를 받는 것이 부류가 아니라 계층 순서로 정해지는지 확인한다. 후보를 부류별로 훑으면
/// 아래에 그려진 입력 필드가 그 위의 버튼을 이겨, 가려진 것이 클릭을 가져간다.
/// </summary>
[[nodiscard]] bool RunHierarchyOrderBeatsKindTests();
