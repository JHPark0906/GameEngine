#pragma once

/// <summary>
/// 버튼·드롭다운·입력 필드가 Selectable의 입력 수신 규칙을 공유하는지 확인한다.
/// RectTransform 요구와 interactable 속성이 기반에 한 번 정의되고 각 요소가 이를 상속해야 한다.
/// </summary>
[[nodiscard]] bool RunSelectableTests();
