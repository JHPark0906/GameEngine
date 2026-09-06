#include "CffFontTests.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "Text/CffFont.h"
#include "Text/FontFace.h"
#include "TestSupport.h"

using GameEngine::Text::ReadFontDictIndex;
using TestSupport::Expect;

/// <summary>
/// CID 키 방식 CFF에서 글리프를 읽고 charstring의 진행폭을 hmtx와 비교한다.
/// CFF는 글리프별 경계 상자를 제공하지 않으므로 진행폭을 독립 검증 기준으로 사용한다.
/// 두 폭이 같아야 charstring의 첫 연산자와 스택·폭 규칙을 올바르게 해석한 것으로 판단한다.
/// </summary>
namespace
{
    [[nodiscard]] std::filesystem::path FontDirectory()
    {
        return std::filesystem::path(__FILE__).parent_path().parent_path() / "GameEditor" /
            "Content" / "Fonts";
    }

    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            return {};
        }
        const std::streamoff size = stream.tellg();
        if (size <= 0)
        {
            return {};
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(bytes.data()), size);
        return stream ? bytes : std::vector<std::byte>{};
    }

    /// <summary>한 CFF 폰트를 열고, 라틴과 한글이 나오는지와 폭이 hmtx와 맞는지 본다.</summary>
    [[nodiscard]] bool CheckFont(const std::string_view fileName)
    {
        const std::filesystem::path path = FontDirectory() / fileName;
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error)
        {
            std::cout << "  cff tests skipped: " << fileName << " is not in this checkout\n";
            return true;
        }
        const std::vector<std::byte> bytes = ReadBytes(path);

        GameEngine::Text::FontFace face;
        if (!Expect(face.Parse(bytes), "the OpenType file parses"))
        {
            return false;
        }
        if (!Expect(
                face.GetOutlineFormat() ==
                    GameEngine::Text::FontFace::OutlineFormat::CompactFontFormat,
                "and declares CFF outlines"))
        {
            return false;
        }

        GameEngine::Text::CffFont cff;
        if (!Expect(cff.Parse(face.GetTable("CFF ")), "and its CFF table parses"))
        {
            return false;
        }

        std::cout << "  " << fileName << ": CID-keyed " << (cff.IsCidKeyed() ? "yes" : "no")
                  << ", " << cff.GetFontDictCount() << " font DICT(s), FDSelect format "
                  << cff.GetFontDictSelectFormat() << ", " << cff.GetGlyphCount()
                  << " charstrings\n";

        bool passed = Expect(
            cff.GetGlyphCount() == face.GetGlyphCount(),
            "and holds exactly as many charstrings as the font says it has glyphs");

        // 라틴 하나와 완성형 한글 셋. 「영문은 나오는데 한글이 안 나온다」를 가르는 자리다.
        for (const char32_t codePoint : { U'A', U'가', U'한', U'힣' })
        {
            const std::uint16_t glyphId = face.GetGlyphIndex(codePoint);
            if (!Expect(glyphId != 0, "the character has a glyph"))
            {
                passed = false;
                continue;
            }
            GameEngine::Text::CharstringResult result;
            if (!Expect(cff.GetGlyphOutline(glyphId, result), "and its charstring interprets"))
            {
                passed = false;
                continue;
            }
            passed &= Expect(!result.outline.IsEmpty(), "and produces contours");

            // 🔑 제2의 진실: charstring이 실은 폭과 hmtx의 진행폭이 같아야 한다.
            const float metricsWidth = static_cast<float>(face.GetAdvanceWidth(glyphId));
            if (std::abs(result.advanceWidth - metricsWidth) > 0.5f)
            {
                std::cerr << "  glyph " << glyphId << ": the charstring says its advance is "
                          << result.advanceWidth << " but hmtx says " << metricsWidth
                          << " — the width was read from the wrong place\n";
            }
            passed &= Expect(
                std::abs(result.advanceWidth - metricsWidth) <= 0.5f,
                "and its charstring width agrees with the horizontal metrics");
        }

        // 공백은 잉크가 없다. 실패가 아니라 답이다.
        GameEngine::Text::CharstringResult space;
        const std::uint16_t spaceGlyph = face.GetGlyphIndex(U' ');
        if (spaceGlyph != 0)
        {
            passed &= Expect(
                cff.GetGlyphOutline(spaceGlyph, space) && space.outline.IsEmpty(),
                "a space interprets to an empty outline rather than a failure");
        }

        // 없는 글리프 번호는 거절한다.
        GameEngine::Text::CharstringResult beyond;
        passed &= Expect(
            !cff.GetGlyphOutline(static_cast<std::uint16_t>(0xFFFF), beyond),
            "and a glyph number past the end is refused");
        return passed;
    }

    /// <summary>
    /// FDSelect의 두 형식을 <b>둘 다</b> 밟는다.
    ///
    /// 동봉 폰트가 한 형식만 쓰면 다른 하나는 제품 코드에서 영영 안 밟히는 갈래가 된다. 손으로
    /// 지은 표를 직접 먹여 두 형식을 다 밟게 한다 — 안 그러면 「읽을 수 있다」고 적어 둔 형식이
    /// 실은 한 번도 확인되지 않은 채 남는다.
    /// </summary>
    [[nodiscard]] bool CheckBothFontDictSelectFormats()
    {
        // 형식 0: 글리프마다 한 바이트.
        const std::vector<std::byte> byGlyph{
            std::byte{ 0 }, std::byte{ 2 }, std::byte{ 0 }, std::byte{ 1 }, std::byte{ 1 } };
        bool passed = true;
        const std::vector<unsigned int> expectedByGlyph{ 2, 0, 1, 1 };
        for (std::uint16_t glyph = 0; glyph < 4; ++glyph)
        {
            unsigned int index = 99;
            const bool read = ReadFontDictIndex(byGlyph, glyph, 4, index);
            passed &= Expect(
                read && index == expectedByGlyph[glyph],
                "format 0 answers the font DICT written for that glyph");
        }

        // 형식 3: 구간 목록. 0..2 -> 5, 3..7 -> 6, 파수꾼 8.
        const std::vector<std::byte> byRange{
            std::byte{ 3 },
            std::byte{ 0 }, std::byte{ 2 },                    // 구간 둘
            std::byte{ 0 }, std::byte{ 0 }, std::byte{ 5 },    // 0부터 -> 5
            std::byte{ 0 }, std::byte{ 3 }, std::byte{ 6 },    // 3부터 -> 6
            std::byte{ 0 }, std::byte{ 8 } };                  // 파수꾼: 8에서 끝
        const std::vector<unsigned int> expectedByRange{ 5, 5, 5, 6, 6, 6, 6, 6 };
        for (std::uint16_t glyph = 0; glyph < 8; ++glyph)
        {
            unsigned int index = 99;
            const bool read = ReadFontDictIndex(byRange, glyph, 8, index);
            if (!read || index != expectedByRange[glyph])
            {
                std::cerr << "  format 3: glyph " << glyph << " answered " << index
                          << " (read=" << read << ") instead of " << expectedByRange[glyph] << "\n";
            }
            passed &= Expect(
                read && index == expectedByRange[glyph],
                "format 3 answers the font DICT for the range the glyph falls in");
        }

        // 파수꾼 밖은 거절한다. 여기서 넘어가면 폰트 DICT 목록 밖을 읽는다.
        unsigned int beyond = 0;
        passed &= Expect(
            !ReadFontDictIndex(byRange, 8, 8, beyond),
            "and a glyph past the sentinel is refused rather than guessed");

        unsigned int unknown = 0;
        const std::vector<std::byte> badFormat{ std::byte{ 7 } };
        passed &= Expect(
            !ReadFontDictIndex(badFormat, 0, 1, unknown),
            "and a format we do not know is refused");
        return passed;
    }
}

bool RunCffFontTests()
{
    bool passed = CheckBothFontDictSelectFormats();
    passed &= CheckFont("MaruBuri-Regular.otf");
    passed &= CheckFont("NanumSquareNeoOTF-Rg.otf");
    return passed;
}

static const TestSupport::Registration gCffFontTests{
    "RenderCache", "cff font tests should pass", RunCffFontTests };
