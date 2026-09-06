#pragma once

/// <summary>
/// Game View와 프로젝트 런타임이 사용하는 sceneTextCache에 RegisterSceneFonts가 폰트를 등록하는지 확인한다.
/// 폰트가 없는 TextRasterizationCache는 모든 텍스트 요청을 거절해야 한다.
/// 등록 후 같은 요청이 성공하는지 확인해 장면 텍스트가 편집기 UI와 독립적으로 폰트를 제공받는지 검증한다.
/// </summary>
[[nodiscard]] bool RunSceneFontRegistrationTests();
