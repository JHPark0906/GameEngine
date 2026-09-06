#pragma once

/// <summary>
/// 페이지 예산을 넘겨도 글리프 UV가 해당 글리프의 잉크를 가리키는지 확인한다.
/// 글리프마다 다른 픽셀 모양을 사용해 다른 글리프나 빈 영역을 가리키는 오류를 구분한다.
/// </summary>
[[nodiscard]] bool RunGlyphAtlasDropoutTests();
