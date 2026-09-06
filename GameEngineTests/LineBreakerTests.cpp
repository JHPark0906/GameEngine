#include "LineBreakerTests.h"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "Text/LineBreaker.h"
#include "TestSupport.h"

using TestSupport::Expect;

/// <summary>
/// 줄바꿈 규칙을 <b>폰트 없이</b> 묻는다.
///
/// 폭은 재는 쪽이 채워 주는 값이므로, 여기서는 글자마다 1.0을 넣고 「몇 글자에서 잘리는가」로
/// 본다. 그러면 규칙이 폰트·크기·힌팅과 무관하게 시험되고, 실패했을 때 그 원인이 규칙 말고는
/// 없다. <c>Core::FitTextToWidth</c>가 같은 이유로 같은 모양을 쓴다.
/// </summary>
namespace
{
    using namespace GameEngine::Text;

    /// <summary>글자마다 폭 1을 준다. 줄 폭이 곧 글자 수가 된다.</summary>
    [[nodiscard]] std::vector<BreakItem> Measure(
        const std::string_view text, const float perCharacter = 1.0f)
    {
        std::vector<BreakItem> items = FindBreakOpportunities(text);
        for (BreakItem& item : items)
        {
            item.advance = item.isMandatoryBreak ? 0.0f : perCharacter;
        }
        return items;
    }

    /// <summary>각 줄이 담은 원문 조각이다. 눈으로 확인할 수 있게 되돌려 낸다.</summary>
    [[nodiscard]] std::vector<std::string> LinesOf(
        const std::string_view text, const float maxWidth, const float perCharacter = 1.0f)
    {
        const std::vector<BreakItem> items = Measure(text, perCharacter);
        const std::vector<LineRange> ranges = BreakIntoLines(items, maxWidth);
        std::vector<std::string> lines;
        for (const LineRange& range : ranges)
        {
            if (range.itemCount == 0)
            {
                lines.emplace_back();
                continue;
            }
            const BreakItem& first = items[range.firstItem];
            const BreakItem& last = items[range.firstItem + range.itemCount - 1];
            lines.emplace_back(
                text.substr(first.byteOffset, last.byteOffset + last.byteLength - first.byteOffset));
        }
        return lines;
    }

    void Report(const char* const what, const std::vector<std::string>& lines)
    {
        std::cout << "  " << what << ":";
        for (const std::string& line : lines)
        {
            std::cout << " [" << line << "]";
        }
        std::cout << "\n";
    }

    [[nodiscard]] bool Check(
        const char* const what,
        const std::vector<std::string>& lines,
        const std::vector<std::string>& expected)
    {
        Report(what, lines);
        if (lines != expected)
        {
            std::cerr << "  " << what << ": expected";
            for (const std::string& line : expected)
            {
                std::cerr << " [" << line << "]";
            }
            std::cerr << "\n";
        }
        return Expect(lines == expected, what);
    }
}

bool RunLineBreakerTests()
{
    bool passed = true;

    // 라틴은 공백에서만 갈라진다. 낱말 한가운데를 자르지 않는다.
    //
    // 줄 끝의 공백은 그 줄의 <b>범위에는 남고</b> 폭에서만 빠진다. 범위에서까지 빼면 바이트
    // 구간이 원문을 덮지 못해, 부르는 쪽이 「이 줄은 어디부터 어디까지인가」를 물었을 때 사이에
    // 구멍이 생긴다. 공백은 잉크가 없으니 남아 있어도 그려지는 것이 없다.
    passed &= Check(
        "latin wraps at spaces", LinesOf("hello brave world", 11.0f),
        { "hello brave ", "world" });

    // 줄 끝의 공백은 폭에 넣지 않는다. 넣으면 정렬이 그만큼 어긋난다.
    {
        const std::vector<BreakItem> items = Measure("ab cd");
        const std::vector<LineRange> lines = BreakIntoLines(items, 3.0f);
        passed &= Expect(lines.size() == 2, "a space at a line end does not push the line over");
        if (lines.size() == 2)
        {
            std::cout << "  trailing space: first line width " << lines[0].width
                      << " (2 expected, not 3)\n";
            passed &= Expect(
                lines[0].width == 2.0f, "and is left out of the width that alignment uses");
        }
    }

    // 한글은 글자마다 갈라진다. 공백만 보면 한 줄이 끝없이 길어진다.
    passed &= Check(
        "hangul wraps between syllables", LinesOf("가나다라마바사", 3.0f),
        { "가나다", "라마바", "사" });

    // 🔴 금칙 하나: 닫는 괄호는 줄 첫머리에 혼자 서지 않는다.
    //
    // 폭 4에 일곱 글자다. 금칙이 없었다면 네 글자를 채우고 「)」 앞에서 잘라 「)라마」가 됐을
    // 것이다 — 닫는 괄호가 줄 머리에 서는 바로 그 모양이다. 금칙이 그 자리와 「(」 다음 자리를
    // 함께 막으므로, 줄바꿈은 그보다 앞의 기회인 「(」 앞으로 물러난다. 그래서 첫 줄이 넷이
    // 아니라 <b>둘</b>이 되는 것이 규칙이 일한 증거다.
    passed &= Check(
        "a closing bracket does not start a line", LinesOf("가나(다)라마", 4.0f),
        { "가나", "(다)라", "마" });

    // 금칙 둘: 여는 괄호는 줄 끝에 남지 않는다.
    passed &= Check(
        "an opening bracket does not end a line", LinesOf("가나(다라마", 3.0f),
        { "가나", "(다라", "마" });

    // 문장부호도 줄 첫머리에 서지 않는다. 한글에서 가장 흔한 금칙이다.
    //
    // 폭을 4로 두는 것이 요점이다. 3으로 두면 「가나다.」가 애초에 들어가지 않아 규칙이 아니라
    // 폭이 답을 정하고, 그러면 금칙이 없어도 같은 결과가 나와 아무것도 증명하지 못한다.
    passed &= Check(
        "a full stop does not start a line", LinesOf("가나다.라", 4.0f),
        { "가나다.", "라" });

    // 명시적 줄바꿈은 폭과 무관하다. 그리고 줄바꿈 문자 자체는 어느 줄에도 남지 않는다.
    passed &= Check(
        "an explicit newline breaks regardless of width", LinesOf("ab\ncd", 100.0f),
        { "ab", "cd" });

    // 폭 0은 「넘침으로는 자르지 않는다」는 뜻이다. 명시적 줄바꿈만 남는다.
    passed &= Check(
        "a width of zero wraps only at explicit newlines", LinesOf("hello brave world", 0.0f),
        { "hello brave world" });

    // 한 덩어리가 혼자서도 넘치면 그 줄에 그대로 둔다. 버리면 글자가 사라진다.
    passed &= Check(
        "a single item wider than the line is kept", LinesOf("abcdef", 3.0f),
        { "abc", "def" });

    // 빈 글자와 UTF-8이 아닌 바이트.
    passed &= Expect(
        FindBreakOpportunities("").empty(), "empty text has no items");
    passed &= Expect(
        FindBreakOpportunities("\xC3\x28").empty(),
        "text that is not UTF-8 yields nothing rather than guessing at it");

    // 섞인 글. 편집기가 실제로 그리는 모양이다 — 라틴 낱말과 한글이 한 줄에 있다.
    Report("mixed", LinesOf("Scene 장면을 저장", 8.0f));
    return passed;
}

static const TestSupport::Registration gLineBreakerTests{
    "Core", "line breaker tests should pass", RunLineBreakerTests };
