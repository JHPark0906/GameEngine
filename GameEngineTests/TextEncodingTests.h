#pragma once

/// <summary>
/// 공통 UTF-16·UTF-8 변환이 허용하는 입력과 거절하는 입력을 확인한다.
/// 잘못된 바이트를 받아 다른 글자로 변환하지 않도록 비정상 입력의 거절을 함께 검증한다.
/// </summary>
[[nodiscard]] bool RunTextEncodingTests();

/// <summary>
/// 이 변환이 한 자리에만 있는지 소스에서 확인한다.
///
/// 통합은 한 번 하면 끝나는 일이 아니다. 다음 사람이 넓은 문자열을 UTF-8로 바꿔야 할 때 가장
/// 가까운 답은 플랫폼 API를 그 자리에서 부르는 것이고, 그렇게 넷째 구현이 생긴다. 그때 붉어지는
/// 것이 이 시험이며, 지키는 것은 「변환이 없다」가 아니라 「변환하는 자리가 하나다」이다.
/// </summary>
[[nodiscard]] bool RunTextEncodingRuleTests();
