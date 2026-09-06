#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GameEngine::Core
{

/// <summary>디코딩된 코드 포인트 하나와, 그것이 원문 어디에서 왔는지다.</summary>
struct Utf8CodePoint
{
    char32_t codePoint = 0;
    /// <summary>원문에서 이 글자가 시작하는 바이트 자리다.</summary>
    std::size_t byteOffset = 0;
    /// <summary>이 글자가 차지하는 바이트 수다. 1에서 4다.</summary>
    std::size_t byteLength = 0;
};

/// <summary>
/// UTF-8을 코드 포인트로 푼다. 바이트 자리를 함께 돌려주므로, 부르는 쪽이 원문의 조각을
/// 다시 가리킬 수 있다 — 줄바꿈이 「어디서 자를까」를 바이트로 답해야 하는 이유가 그것이다.
///
/// 값이 없으면 입력이 UTF-8이 아니다. 판정은 <see cref="Utf8ToUtf16"/>과 <b>같은 것</b>이며,
/// 실제로 그 함수가 이것을 쓴다 — 디코더가 둘이면 한쪽이 받아들이는 것을 다른 쪽이 거절하는
/// 날이 오고, 그때 같은 문자열이 어느 길로 갔느냐에 따라 다르게 취급된다.
/// </summary>
[[nodiscard]] std::optional<std::vector<Utf8CodePoint>> DecodeUtf8(std::string_view text);

/// <summary>
/// UTF-16을 UTF-8로 옮긴다.
///
/// 이름이 <c>char16_t</c>인 이유는 그것이 인코딩의 이름이기 때문이다. 플랫폼의 넓은 문자 타입은
/// 어디서는 UTF-16이고 어디서는 UTF-32라, 그것을 계약에 적으면 이 함수가 무엇을 받는지가
/// 플랫폼마다 달라진다. 넓은 문자열을 쥔 쪽은 두 바이트짜리 요소를 그대로 옮겨 담아 부른다 —
/// Windows에서 그 둘은 같은 비트이고, 그 사실을 아는 것은 플랫폼을 아는 코드의 몫이다.
///
/// 짝을 잃은 서로게이트는 버린다. 반쪽짜리 문자는 글자가 아니고, 그것이 실제로 도착하는 자리가
/// 있다 — Win32의 WM_CHAR는 보조 평면 문자를 두 번에 나누어 보내므로, 짝이 오기 전의 한쪽은
/// 그 자체로는 옮길 것이 없다.
/// </summary>
[[nodiscard]] std::string Utf16ToUtf8(std::u16string_view text);

/// <summary>
/// UTF-8을 UTF-16으로 옮긴다.
///
/// 값이 없으면 입력이 UTF-8이 아니다 — 잘린 시퀀스, 과장 부호화, 서로게이트 값, 범위를 넘는
/// 코드 포인트가 모두 여기 든다. 실패를 값으로 돌려주는 이유는 부르는 쪽마다 답이 다르기
/// 때문이다: 어떤 자리는 빈 문자열로 넘어가고, 어떤 자리는 무엇이 잘못됐는지 자기 문맥과 함께
/// 말해야 한다. 이 함수가 그것을 대신 정하면 둘 중 하나는 틀린 말을 하게 된다.
/// </summary>
[[nodiscard]] std::optional<std::u16string> Utf8ToUtf16(std::string_view text);

}
