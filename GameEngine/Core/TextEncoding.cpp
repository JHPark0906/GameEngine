#include "pch.h"
#include "TextEncoding.h"

#include <cstddef>
#include <cstdint>

namespace GameEngine::Core
{

namespace
{
    // 유니코드가 정한 경계들이다. 이름을 붙여 두는 것은 아래의 검사들이 무엇을 거르는지가 숫자만
    // 봐서는 읽히지 않기 때문이다.
    constexpr char32_t MaximumCodePoint = 0x10FFFF;
    constexpr char32_t FirstSurrogate = 0xD800;
    constexpr char32_t LastSurrogate = 0xDFFF;
    constexpr char32_t FirstLowSurrogate = 0xDC00;
    constexpr char32_t FirstSupplementary = 0x10000;

    [[nodiscard]] constexpr bool IsSurrogate(const char32_t codePoint)
    {
        return codePoint >= FirstSurrogate && codePoint <= LastSurrogate;
    }

    /// <summary>코드 포인트 하나를 UTF-8 바이트로 이어 붙인다.</summary>
    void AppendUtf8(std::string& destination, const char32_t codePoint)
    {
        const auto byte = [&destination](const std::uint32_t value)
        {
            destination.push_back(static_cast<char>(static_cast<unsigned char>(value)));
        };
        if (codePoint < 0x80)
        {
            byte(codePoint);
        }
        else if (codePoint < 0x800)
        {
            byte(0xC0 | (codePoint >> 6));
            byte(0x80 | (codePoint & 0x3F));
        }
        else if (codePoint < FirstSupplementary)
        {
            byte(0xE0 | (codePoint >> 12));
            byte(0x80 | ((codePoint >> 6) & 0x3F));
            byte(0x80 | (codePoint & 0x3F));
        }
        else
        {
            byte(0xF0 | (codePoint >> 18));
            byte(0x80 | ((codePoint >> 12) & 0x3F));
            byte(0x80 | ((codePoint >> 6) & 0x3F));
            byte(0x80 | (codePoint & 0x3F));
        }
    }

    /// <summary>
    /// UTF-16 단위 하나를 읽어 코드 포인트를 얻는다. <c>index</c>는 읽은 만큼 나아간다.
    /// 값이 없으면 짝을 잃은 서로게이트다.
    /// </summary>
    [[nodiscard]] std::optional<char32_t> ReadUtf16(
        const std::u16string_view text, std::size_t& index)
    {
        const auto unit = static_cast<char32_t>(text[index]);
        ++index;
        if (!IsSurrogate(unit))
        {
            return unit;
        }
        if (unit >= FirstLowSurrogate || index >= text.size())
        {
            // 낮은 쪽이 먼저 왔거나 짝이 아예 없다. 반쪽은 글자가 아니다.
            return std::nullopt;
        }
        const auto low = static_cast<char32_t>(text[index]);
        if (low < FirstLowSurrogate || low > LastSurrogate)
        {
            return std::nullopt;
        }
        ++index;
        return FirstSupplementary + ((unit - FirstSurrogate) << 10) + (low - FirstLowSurrogate);
    }

    /// <summary>코드 포인트 하나를 UTF-16으로 이어 붙인다.</summary>
    void AppendUtf16(std::u16string& destination, const char32_t codePoint)
    {
        if (codePoint >= FirstSupplementary)
        {
            const char32_t offset = codePoint - FirstSupplementary;
            destination.push_back(static_cast<char16_t>(FirstSurrogate + (offset >> 10)));
            destination.push_back(static_cast<char16_t>(FirstLowSurrogate + (offset & 0x3FF)));
            return;
        }
        destination.push_back(static_cast<char16_t>(codePoint));
    }
}

std::string Utf16ToUtf8(const std::u16string_view text)
{
    std::string utf8;
    utf8.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size())
    {
        if (const std::optional<char32_t> codePoint = ReadUtf16(text, index))
        {
            AppendUtf8(utf8, *codePoint);
        }
    }
    return utf8;
}

std::optional<std::vector<Utf8CodePoint>> DecodeUtf8(const std::string_view text)
{
    std::vector<Utf8CodePoint> decoded;
    decoded.reserve(text.size());
    for (std::size_t index = 0; index < text.size();)
    {
        const std::size_t start = index;
        const auto lead = static_cast<unsigned char>(text[index]);
        std::size_t continuationCount = 0;
        char32_t codePoint = 0;
        char32_t smallest = 0;
        if (lead < 0x80)
        {
            codePoint = lead;
        }
        else if ((lead & 0xE0) == 0xC0)
        {
            continuationCount = 1;
            codePoint = lead & 0x1F;
            smallest = 0x80;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            continuationCount = 2;
            codePoint = lead & 0x0F;
            smallest = 0x800;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            continuationCount = 3;
            codePoint = lead & 0x07;
            smallest = FirstSupplementary;
        }
        else
        {
            // 0x80..0xBF는 이어지는 바이트인데 첫 자리에 왔고, 0xF8 이상은 어떤 시퀀스의
            // 시작도 아니다.
            return std::nullopt;
        }

        if (continuationCount > 0 && index + continuationCount >= text.size())
        {
            // 잘린 시퀀스다. 뒤가 없는 것을 채워 읽으면 없는 글자를 지어내게 된다.
            return std::nullopt;
        }
        for (std::size_t offset = 1; offset <= continuationCount; ++offset)
        {
            const auto continuation = static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xC0) != 0x80)
            {
                return std::nullopt;
            }
            codePoint = (codePoint << 6) | (continuation & 0x3F);
        }
        index += continuationCount + 1;

        // 과장 부호화(짧게 쓸 수 있는 것을 길게 쓴 것)는 같은 글자를 두 가지로 적을 수 있게
        // 해서, 검사를 통과한 뒤 다른 것이 되는 길을 연다. 서로게이트 값과 범위를 넘는 값은
        // UTF-8에 애초에 없다.
        if (codePoint < smallest || IsSurrogate(codePoint) || codePoint > MaximumCodePoint)
        {
            return std::nullopt;
        }
        decoded.push_back(Utf8CodePoint{ codePoint, start, index - start });
    }
    return decoded;
}

std::optional<std::u16string> Utf8ToUtf16(const std::string_view text)
{
    // 디코딩은 위의 한 곳에서만 한다. 여기서 두 번째 해석기를 쓰면 무엇이 UTF-8인지에 대한
    // 답이 둘이 되고, 그 둘은 언젠가 갈라진다.
    const std::optional<std::vector<Utf8CodePoint>> decoded = DecodeUtf8(text);
    if (!decoded)
    {
        return std::nullopt;
    }
    std::u16string utf16;
    utf16.reserve(text.size());
    for (const Utf8CodePoint& character : *decoded)
    {
        AppendUtf16(utf16, character.codePoint);
    }
    return utf16;
}

}
