#pragma once

/// <summary>
/// Core::TextEditModel의 캐럿·선택·편집 규칙을 검증한다. 특히 한 문자가 여러 바이트인
/// 텍스트에서 캐럿이 문자 단위로 움직이는지를 본다.
/// </summary>
[[nodiscard]] bool RunTextEditModelTests();
