#pragma once

/// <summary>
/// Transform과 SpriteRenderer만 있고 화면 사각형이 없는 버튼은 커서를 받지 못해야 한다.
/// 완전한 UI 계층에 둔 Button은 입력을 받는 대조군으로 함께 확인한다.
/// </summary>
[[nodiscard]] bool RunWorldButtonReachTests();
