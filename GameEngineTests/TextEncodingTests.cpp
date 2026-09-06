#include "TextEncodingTests.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "Core/TextEncoding.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>바이트 값을 그대로 적어 문자열을 만든다. 잘못된 UTF-8을 손으로 세우는 자리다.</summary>
    [[nodiscard]] std::string Bytes(const std::initializer_list<int> values)
    {
        std::string text;
        for (const int value : values)
        {
            text.push_back(static_cast<char>(static_cast<unsigned char>(value)));
        }
        return text;
    }
}

bool RunTextEncodingTests()
{
    using GameEngine::Core::Utf16ToUtf8;
    using GameEngine::Core::Utf8ToUtf16;
    // 넓은 문자열을 쥔 쪽이 하는 것과 같은 옮겨 담기다 — Windows에서 그 둘은 같은 비트다.
    const auto asUtf16 = [](const std::wstring& text)
    {
        return std::u16string(text.begin(), text.end());
    };

    // 왕복. 아스키, 한글(3바이트), 그리고 보조 평면(4바이트, Windows에서는 서로게이트 쌍).
    const std::wstring wide = L"Game 엔진 \U0001F600";
    const std::string utf8 = Utf16ToUtf8(asUtf16(wide));
    const bool roundTrips = Utf8ToUtf16(utf8) == asUtf16(wide);
    // 바이트 수가 형식대로다. 문자열이 같은지만 보면 양쪽이 같은 방식으로 틀려도 통과한다.
    const bool encodesByTheRules = Utf16ToUtf8(u"A") == "A" && Utf16ToUtf8(u"é").size() == 2 &&
        Utf16ToUtf8(u"엔").size() == 3 && Utf16ToUtf8(u"\U0001F600").size() == 4;
    const bool emptyStaysEmpty = Utf16ToUtf8(u"").empty() && Utf8ToUtf16("") == std::u16string{};

    // 🔴 거절해야 하는 것들. 이 중 하나라도 통과하면 그 바이트는 나중에 다른 글자가 된다.
    const bool rejectsTruncated = !Utf8ToUtf16(Bytes({ 0xE1, 0x88 }));
    const bool rejectsLoneContinuation = !Utf8ToUtf16(Bytes({ 0x80 }));
    const bool rejectsBadContinuation = !Utf8ToUtf16(Bytes({ 0xE1, 0x88, 0x41 }));
    // 과장 부호화: "/"를 두 바이트로 적은 것이다. 받아 주면 경로 검사를 지나친 뒤 "/"가 된다.
    const bool rejectsOverlong = !Utf8ToUtf16(Bytes({ 0xC0, 0xAF })) &&
        !Utf8ToUtf16(Bytes({ 0xE0, 0x80, 0xAF }));
    // UTF-8에 서로게이트 값은 없다. UTF-16의 표현 수단이지 문자가 아니다.
    const bool rejectsSurrogate = !Utf8ToUtf16(Bytes({ 0xED, 0xA0, 0x80 }));
    // U+10FFFF를 넘는 값과, 어떤 시퀀스의 시작도 아닌 바이트.
    const bool rejectsOutOfRange = !Utf8ToUtf16(Bytes({ 0xF4, 0x90, 0x80, 0x80 })) &&
        !Utf8ToUtf16(Bytes({ 0xF8, 0x88, 0x80, 0x80, 0x80 }));
    // 경계는 받는다. 거절이 지나치면 멀쩡한 글자가 사라진다.
    const bool acceptsTheEdges = Utf8ToUtf16(Bytes({ 0xF4, 0x8F, 0xBF, 0xBF })).has_value() &&
        Utf8ToUtf16(Bytes({ 0x7F })).has_value() && Utf8ToUtf16(Bytes({ 0xC2, 0x80 })).has_value();

    // 🔴 짝을 잃은 서로게이트는 버린다. 반쪽은 글자가 아니고, Win32의 WM_CHAR가 보조 평면
    // 문자를 두 번에 나누어 보내므로 짝이 오기 전의 한쪽이 실제로 이 자리에 도착한다.
    const std::u16string loneHigh(1, static_cast<char16_t>(0xD83D));
    const std::u16string loneLow(1, static_cast<char16_t>(0xDE00));
    const bool dropsHalfCharacters = Utf16ToUtf8(loneHigh).empty() &&
        Utf16ToUtf8(loneLow).empty() && Utf16ToUtf8(loneHigh + u"A") == "A";

    return Expect(roundTrips, "text should survive a trip out to UTF-8 and back") &&
        Expect(encodesByTheRules, "each code point should take the number of bytes UTF-8 gives it") &&
        Expect(emptyStaysEmpty, "an empty string should convert to an empty string") &&
        Expect(rejectsTruncated, "a sequence that ends early should be rejected") &&
        Expect(rejectsLoneContinuation, "a continuation byte on its own should be rejected") &&
        Expect(rejectsBadContinuation, "a sequence whose continuation is not one should be rejected") &&
        Expect(rejectsOverlong, "an overlong encoding should be rejected") &&
        Expect(rejectsSurrogate, "a surrogate value has no place in UTF-8 and should be rejected") &&
        Expect(rejectsOutOfRange, "a code point past U+10FFFF should be rejected") &&
        Expect(acceptsTheEdges, "the highest and lowest valid sequences should be accepted") &&
        Expect(dropsHalfCharacters, "an unpaired surrogate should be dropped, not encoded");
}

bool RunTextEncodingRuleTests()
{
    namespace fs = std::filesystem;

    // 이 파일은 <repo>/GameEngineTests/TextEncodingTests.cpp에 있다.
    const fs::path repository = fs::path(__FILE__).parent_path().parent_path();
    std::error_code error;
    if (!fs::is_directory(repository, error))
    {
        std::cout << "  text encoding rule test skipped: sources not found\n";
        return true;
    }

    // 플랫폼별 변환 API 대신 공통 텍스트 인코딩 경로를 사용하는지 검사한다.
    static constexpr std::array<std::string_view, 2> PlatformConversions{
        "WideCharToMultiByte", "MultiByteToWideChar" };
    static constexpr std::array<std::string_view, 3> ScannedDirectories{
        "GameEngine", "GameEditor", "GameBuilder" };

    // 코드페이지를 유니코드로 옮기는 자리 하나는 뺀다. 이 규칙이 없애려는 것은 UTF-16과
    // UTF-8 사이의 변환이 여러 곳에 흩어지는 것이고, 그 둘은 같은 문자 집합을 다르게 적은
    // 것이라 표 없이 계산으로 오간다. 콘솔이 쓰는 코드페이지는 유니코드가 아니어서 —
    // 한국어 Windows의 CP949 — 어느 바이트가 어느 글자인지가 운영체제의 표에만 있고,
    // 표준 라이브러리로는 옮길 수 없다. 그래서 그 변환만은 플랫폼에게 묻되, 묻는 자리를
    // 이 파일 하나로 묶어 두고 여기 이름을 적는다.
    static constexpr std::array<std::string_view, 1> CodePageDecoders{
        // UTF 변환이 아니라 콘솔 코드페이지 디코딩이다. 표준 라이브러리가 답할 수 없어 예외.
        "GameEngine/Platform/Win32/Win32ConsoleText.cpp" };

    std::vector<std::string> found;
    for (const std::string_view directory : ScannedDirectories)
    {
        for (const auto& entry : fs::recursive_directory_iterator(repository / directory, error))
        {
            if (error || !entry.is_regular_file())
            {
                continue;
            }
            const fs::path& path = entry.path();
            if (path.extension() != ".cpp" && path.extension() != ".h")
            {
                continue;
            }
            const std::string relative =
                fs::relative(path, repository, error).generic_string();
            if (std::ranges::find(CodePageDecoders, relative) != CodePageDecoders.end())
            {
                continue;
            }
            std::ifstream stream(path);
            std::string line;
            unsigned int lineNumber = 0;
            while (std::getline(stream, line))
            {
                ++lineNumber;
                const std::size_t firstCharacter = line.find_first_not_of(" \t");
                if (firstCharacter != std::string::npos &&
                    line.compare(firstCharacter, 2, "//") == 0)
                {
                    // 주석이 규칙을 설명하려면 그 이름을 부를 수 있어야 한다.
                    continue;
                }
                for (const std::string_view name : PlatformConversions)
                {
                    if (line.find(name) != std::string::npos)
                    {
                        found.push_back(
                            fs::relative(path, repository, error).generic_string() + ":" +
                            std::to_string(lineNumber) + " calls " + std::string(name));
                    }
                }
            }
        }
    }

    for (const std::string& call : found)
    {
        std::cerr << "  text encoding: " << call << "\n";
    }
    return Expect(
        found.empty(),
        "the platform's conversion API should be called nowhere; Core::Utf16ToUtf8 and "
        "Core::Utf8ToUtf16 are where this conversion lives");
}

static const TestSupport::Registration gTextEncodingTests{
    "Core", "text encoding tests should pass", RunTextEncodingTests };

static const TestSupport::Registration gTextEncodingRuleTests{
    "Core", "text encoding rule tests should pass", RunTextEncodingRuleTests };
