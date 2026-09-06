#pragma once

#include <string>
#include <string_view>

namespace GameEngine::Platform
{

/// <summary>
/// 콘솔 프로그램이 낸 바이트를 UTF-8로 옮긴다.
///
/// 이것은 <see cref="Core::Utf16ToUtf8"/>이 하는 일과 다른 일이다. 저기서 옮기는 두 인코딩은
/// 같은 문자 집합을 다르게 적은 것이라 표(table) 없이 계산으로 오갈 수 있지만, 콘솔이 쓰는
/// 코드페이지 — 한국어 Windows의 CP949 — 는 유니코드가 아니고, 어느 바이트가 어느 글자인지는
/// 운영체제가 쥔 표에만 있다. 그래서 이 변환만은 플랫폼에게 물어야 하고, 물어보는 자리를 이
/// 파일 하나로 둔다.
///
/// 자식이 쓸 코드페이지는 시스템의 OEM 코드페이지로 본다. 이 프로세스의 콘솔 코드페이지가
/// 아니다: 그 값은 에디터를 띄운 셸이 정한 것이라 자식과 상관이 없고, 실제로 같은 실행 파일이
/// 셸에 따라 다른 글자를 읽게 만든다.
/// </summary>
[[nodiscard]] std::string ConsoleBytesToUtf8(std::string_view bytes);

}
