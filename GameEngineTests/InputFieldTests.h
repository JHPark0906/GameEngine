#pragma once

/// <summary>
/// 유지 모드 InputField가 포커스를 받고, 그 포커스가 있는 동안의 타이핑·선택·클립보드를 편집
/// 모델에 넘기고, 보이는 글자를 TextRenderer에 싣는지 확인한다.
/// </summary>
[[nodiscard]] bool RunInputFieldTests();
