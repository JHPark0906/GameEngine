#include "pch.h"
#include "Guid.h"

#include <array>
#include <cstddef>
#include <random>

namespace GameEngine::Core
{

namespace
{
    constexpr std::size_t TextLength = 32;

    /// <summary>16진수 한 글자의 값이다. 16진수가 아니면 비어 있다.</summary>
    [[nodiscard]] constexpr std::optional<std::uint64_t> HexValue(const char character)
    {
        if (character >= '0' && character <= '9')
        {
            return static_cast<std::uint64_t>(character - '0');
        }
        if (character >= 'a' && character <= 'f')
        {
            return static_cast<std::uint64_t>(character - 'a') + 10;
        }
        if (character >= 'A' && character <= 'F')
        {
            return static_cast<std::uint64_t>(character - 'A') + 10;
        }
        return std::nullopt;
    }

    void AppendHex(std::string& text, const std::uint64_t value)
    {
        constexpr char digits[] = "0123456789abcdef";
        for (int shift = 60; shift >= 0; shift -= 4)
        {
            text += digits[(value >> shift) & 0xFull];
        }
    }
}

std::string Guid::ToString() const
{
    if (!IsValid())
    {
        return {};
    }
    std::string text;
    text.reserve(TextLength);
    AppendHex(text, high);
    AppendHex(text, low);
    return text;
}

bool Guid::LooksLikeGuid(const std::string_view text)
{
    if (text.size() != TextLength)
    {
        return false;
    }
    for (const char character : text)
    {
        if (!HexValue(character))
        {
            return false;
        }
    }
    return true;
}

std::optional<Guid> Guid::Parse(const std::string_view text)
{
    if (!LooksLikeGuid(text))
    {
        return std::nullopt;
    }
    Guid guid;
    for (std::size_t index = 0; index < TextLength; ++index)
    {
        const std::optional<std::uint64_t> digit = HexValue(text[index]);
        std::uint64_t& half = index < 16 ? guid.high : guid.low;
        half = (half << 4) | *digit;
    }
    return guid.IsValid() ? std::optional<Guid>(guid) : std::nullopt;
}

Guid MakeGuid()
{
    // 하드웨어 엔트로피로 씨를 뿌린 생성기 하나를 계속 쓴다. 발급이 잦지 않으므로 값싸야 할
    // 이유는 없지만, 매번 새로 뿌리면 같은 밀리초에 만든 둘이 같은 값을 받을 수 있다.
    static std::mt19937_64 generator{ std::random_device{}() };
    static std::uniform_int_distribution<std::uint64_t> distribution;

    Guid guid;
    guid.high = distribution(generator);
    guid.low = distribution(generator);
    // 버전 4와 변형 비트를 박는다. 이 값이 UUID로 읽히는 곳에서 무엇인지 말해 주며, 두 반쪽이
    // 모두 0이 되는 일 — 「없음」과 구별되지 않는 값 — 도 이것으로 사라진다.
    guid.high = (guid.high & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
    guid.low = (guid.low & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
    return guid;
}

}
