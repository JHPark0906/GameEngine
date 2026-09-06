#include "pch.h"
#include "LineBreaker.h"

#include "../Core/TextEncoding.h"

#include <algorithm>
#include <optional>
#include <string_view>

namespace GameEngine::Text
{

namespace
{
    [[nodiscard]] bool IsSpace(const char32_t codePoint)
    {
        // 줄바꿈 문자는 공백으로 치지 않는다 — 따로 다뤄야 하기 때문이다.
        return codePoint == U' ' || codePoint == U'\t' ||
            codePoint == 0x3000;  // 전각 공백. 한글 문서에 실제로 온다.
    }

    /// <summary>
    /// 글자마다 자를 수 있는 문자인가.
    ///
    /// 한글 완성형과 자모, 한자, 가나, 그리고 전각 문장부호가 여기 든다. 이들은 낱말 사이에
    /// 공백을 두지 않고 이어 쓰는 일이 흔해서, 공백만 보고 자르면 한 줄이 끝없이 길어진다.
    /// </summary>
    [[nodiscard]] bool IsIdeographic(const char32_t codePoint)
    {
        return (codePoint >= 0xAC00 && codePoint <= 0xD7A3) ||    // 한글 완성형
            (codePoint >= 0x1100 && codePoint <= 0x11FF) ||       // 한글 자모
            (codePoint >= 0x3130 && codePoint <= 0x318F) ||       // 호환 자모
            (codePoint >= 0x4E00 && codePoint <= 0x9FFF) ||       // 한자
            (codePoint >= 0x3400 && codePoint <= 0x4DBF) ||       // 한자 확장 A
            (codePoint >= 0x3040 && codePoint <= 0x30FF) ||       // 가나
            (codePoint >= 0xFF01 && codePoint <= 0xFF60);         // 전각 문장부호
    }

    /// <summary>
    /// 줄 첫머리에 오면 안 되는 문자인가.
    ///
    /// 닫는 괄호와 문장부호다. 이것들이 줄 앞에 혼자 떨어지는 것이 한글 조판에서 가장 눈에
    /// 띄는 잘못이고, 「음절마다 자를 수 있다」를 규칙 그대로 쓰면 반드시 일어난다.
    /// </summary>
    [[nodiscard]] bool ForbidsBreakBefore(const char32_t codePoint)
    {
        switch (codePoint)
        {
        case U')': case U']': case U'}': case U'>':
        case U'.': case U',': case U'!': case U'?': case U':': case U';':
        case U'\'': case U'"':
        case 0x3009: case 0x300B: case 0x300D: case 0x300F: case 0x3011:  // 〉》」』】
        case 0x201D: case 0x2019:                                          // ” ’
        case 0xFF09: case 0xFF3D: case 0xFF5D:                             // 전각 ) ] }
        case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B:                // 전각 , . : ;
        case 0xFF01: case 0xFF1F:                                          // 전각 ! ?
            return true;
        default:
            return false;
        }
    }

    /// <summary>줄 끝에 오면 안 되는 문자인가. 여는 괄호와 따옴표다.</summary>
    [[nodiscard]] bool ForbidsBreakAfter(const char32_t codePoint)
    {
        switch (codePoint)
        {
        case U'(': case U'[': case U'{': case U'<':
        case 0x3008: case 0x300A: case 0x300C: case 0x300E: case 0x3010:  // 〈《「『【
        case 0x201C: case 0x2018:                                          // “ ‘
        case 0xFF08: case 0xFF3B: case 0xFF5B:                             // 전각 ( [ {
            return true;
        default:
            return false;
        }
    }
}

std::vector<BreakItem> FindBreakOpportunities(const std::string_view text)
{
    const std::optional<std::vector<Core::Utf8CodePoint>> decoded = Core::DecodeUtf8(text);
    if (!decoded)
    {
        return {};
    }

    std::vector<BreakItem> items;
    items.reserve(decoded->size());
    for (std::size_t index = 0; index < decoded->size(); ++index)
    {
        const Core::Utf8CodePoint& character = (*decoded)[index];
        BreakItem item;
        item.byteOffset = character.byteOffset;
        item.byteLength = character.byteLength;
        item.codePoint = character.codePoint;
        item.isSpace = IsSpace(character.codePoint);
        item.isMandatoryBreak = character.codePoint == U'\n';

        if (index == 0)
        {
            // 첫 글자 앞에서 자르는 것은 줄을 비우는 것이라 뜻이 없다.
            items.push_back(item);
            continue;
        }

        const char32_t previous = (*decoded)[index - 1].codePoint;
        const char32_t current = character.codePoint;

        // 공백 뒤에서는 자를 수 있다. 라틴 글자가 갈라지는 자리는 사실상 여기뿐이다.
        bool canBreak = IsSpace(previous);
        // 한글·한자는 글자마다 갈라진다. 양쪽 중 하나라도 그런 문자면 그 경계가 기회다.
        canBreak = canBreak || IsIdeographic(previous) || IsIdeographic(current);
        // 금칙이 그 기회를 도로 거둔다. 닫는 부호가 줄 앞에 서거나 여는 부호가 줄 끝에 남는
        // 것을 막는다.
        canBreak = canBreak && !ForbidsBreakBefore(current) && !ForbidsBreakAfter(previous);

        item.canBreakBefore = canBreak;
        items.push_back(item);
    }
    return items;
}

std::vector<LineRange> BreakIntoLines(
    const std::span<const BreakItem> items, const float maxWidth)
{
    std::vector<LineRange> lines;
    if (items.empty())
    {
        return lines;
    }

    std::size_t lineStart = 0;
    float lineWidth = 0.0f;
    // 줄 끝의 공백은 폭에 넣지 않는다. 넣으면 오른쪽 정렬한 글이 공백만큼 왼쪽으로 밀리고,
    // 가운데 정렬은 공백의 절반만큼 어긋난다.
    float widthWithoutTrailingSpaces = 0.0f;
    std::size_t lastBreakCandidate = 0;
    bool hasBreakCandidate = false;
    float widthAtCandidate = 0.0f;

    const auto endLine = [&](const std::size_t endIndex, const float width)
    {
        lines.push_back(LineRange{ lineStart, endIndex - lineStart, width });
    };

    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const BreakItem& item = items[index];

        if (item.isMandatoryBreak)
        {
            // 줄바꿈 문자는 그 줄에 남기지 않고 여기서 끊는다.
            endLine(index, widthWithoutTrailingSpaces);
            lineStart = index + 1;
            lineWidth = 0.0f;
            widthWithoutTrailingSpaces = 0.0f;
            hasBreakCandidate = false;
            continue;
        }

        if (index > lineStart && item.canBreakBefore)
        {
            lastBreakCandidate = index;
            widthAtCandidate = widthWithoutTrailingSpaces;
            hasBreakCandidate = true;
        }

        const float nextWidth = lineWidth + item.advance;
        const bool overflows =
            maxWidth > 0.0f && nextWidth > maxWidth && index > lineStart && !item.isSpace;
        if (overflows)
        {
            if (hasBreakCandidate)
            {
                endLine(lastBreakCandidate, widthAtCandidate);
                // 자른 자리부터 다시 잰다. 그 사이의 글자들은 새 줄의 것이다.
                lineStart = lastBreakCandidate;
                lineWidth = 0.0f;
                widthWithoutTrailingSpaces = 0.0f;
                hasBreakCandidate = false;
                for (std::size_t back = lineStart; back < index; ++back)
                {
                    lineWidth += items[back].advance;
                    if (!items[back].isSpace)
                    {
                        widthWithoutTrailingSpaces = lineWidth;
                    }
                }
            }
            else
            {
                // 자를 자리가 없다. 넘치는 채로 끊는 수밖에 없고, 여기서 자르면 낱말 한가운데다 —
                // 그것이 글자를 잃는 것보다는 낫다.
                endLine(index, widthWithoutTrailingSpaces);
                lineStart = index;
                lineWidth = 0.0f;
                widthWithoutTrailingSpaces = 0.0f;
            }
        }

        lineWidth += item.advance;
        if (!item.isSpace)
        {
            widthWithoutTrailingSpaces = lineWidth;
        }
    }

    endLine(items.size(), widthWithoutTrailingSpaces);
    return lines;
}

}
