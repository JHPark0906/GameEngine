#include "FontFaceTests.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "Text/FontFace.h"
#include "TestSupport.h"

using TestSupport::Expect;

/// <summary>
/// 번들 폰트의 실제 바이트를 읽어 검사하며 시스템 폰트 폴백에 의존하지 않는다.
/// </summary>
namespace
{
    /// <summary>이 시험은 <repo>/GameEngineTests에 있다.</summary>
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
        if (!stream)
        {
            return {};
        }
        return bytes;
    }

    /// <summary>한 폰트를 열고, 형식·지표·글자 대응을 함께 묻는다.</summary>
    [[nodiscard]] bool CheckFont(
        const std::string_view fileName,
        const GameEngine::Text::FontFace::OutlineFormat expectedFormat,
        const bool expectHangul)
    {
        const std::filesystem::path path = FontDirectory() / fileName;
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error)
        {
            std::cout << "  font face tests skipped: " << fileName << " is not in this checkout\n";
            return true;
        }

        const std::vector<std::byte> bytes = ReadBytes(path);
        if (!Expect(!bytes.empty(), "the font file can be read"))
        {
            return false;
        }

        GameEngine::Text::FontFace face;
        if (!Expect(face.Parse(bytes), (std::string(fileName) + " parses").c_str()))
        {
            return false;
        }

        bool passed = true;
        passed &= Expect(
            face.GetOutlineFormat() == expectedFormat,
            "the outline format is the one the file declares");
        passed &= Expect(face.GetUnitsPerEm() > 0, "and it says how many units make an em");
        passed &= Expect(face.GetGlyphCount() > 1, "and it has glyphs");

        // 'A'는 어느 폰트에나 있다. 0이 나오면 cmap format 4의 자리 계산이 틀린 것이고, 그
        // 계산은 idRangeOffset이 자기 슬롯에서부터 세는 상대 거리라 가장 자주 틀리는 곳이다.
        const std::uint16_t latin = face.GetGlyphIndex(U'A');
        if (latin == 0)
        {
            std::cerr << "  " << fileName << ": 'A' maps to .notdef, so the character map is "
                      << "not being read correctly\n";
        }
        passed &= Expect(latin != 0, "and 'A' finds a glyph");
        passed &= Expect(
            face.GetAdvanceWidth(latin) > 0, "and that glyph has an advance width");

        // 없는 글자는 0이다. 실패가 아니라 답이며, 대체 폰트를 찾을지 두부를 그릴지는 부르는
        // 쪽이 정한다. 0xE000은 사용자 정의 영역이라 본문 폰트가 채워 둘 이유가 없다.
        passed &= Expect(
            face.GetGlyphIndex(static_cast<char32_t>(0xE000)) == 0,
            "and a character the font does not have answers .notdef rather than failing");

        if (expectHangul)
        {
            // 완성형 한글 셋. 한글이 나와야 한다는 것이 이 프로젝트의 합격 조건이고, 영문만
            // 확인하는 시험은 그 조건을 지키지 못한다.
            for (const char32_t syllable : { U'가', U'한', U'힣' })
            {
                const std::uint16_t glyph = face.GetGlyphIndex(syllable);
                if (glyph == 0)
                {
                    std::cerr << "  " << fileName << ": U+" << std::hex
                              << static_cast<unsigned int>(syllable) << std::dec
                              << " maps to .notdef\n";
                }
                passed &= Expect(glyph != 0, "and a Hangul syllable finds a glyph");
            }
        }

        const GameEngine::Text::FontFace::VerticalMetrics& metrics = face.GetVerticalMetrics();
        std::cout << "  " << fileName << ": unitsPerEm=" << face.GetUnitsPerEm()
                  << " glyphs=" << face.GetGlyphCount() << " 'A'=" << latin
                  << " hhea asc/desc/gap=" << metrics.hheaAscender << "/"
                  << metrics.hheaDescender << "/" << metrics.hheaLineGap;
        if (metrics.hasOs2)
        {
            std::cout << " typo=" << metrics.typoAscender << "/" << metrics.typoDescender << "/"
                      << metrics.typoLineGap << " win=" << metrics.winAscent << "/"
                      << metrics.winDescent;
        }
        std::cout << "\n";

        passed &= Expect(
            metrics.hheaAscender > 0 && metrics.hheaDescender < 0,
            "and the vertical metrics put the ascender above the baseline and the descender below");
        return passed;
    }

    /// <summary>폰트가 아닌 바이트는 거절하고, 그러면서 죽지 않는다.</summary>
    [[nodiscard]] bool CheckRubbishIsRefused()
    {
        bool passed = true;
        GameEngine::Text::FontFace face;

        passed &= Expect(!face.Parse({}), "an empty file is not a font");

        const std::vector<std::byte> notAFont(64, std::byte{ 0x7F });
        passed &= Expect(!face.Parse(notAFont), "and neither is a block of filler");

        // 진짜 폰트를 잘라 낸 것. 헤더는 그럴듯하고 테이블은 파일 밖을 가리킨다 — 손상된
        // 파일이 이 코드를 배열 밖으로 데려가지 않는지 묻는 자리다.
        const std::filesystem::path path = FontDirectory() / "MaruBuri-Regular.otf";
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error) && !error)
        {
            std::vector<std::byte> truncated = ReadBytes(path);
            if (truncated.size() > 256)
            {
                truncated.resize(256);
                passed &= Expect(
                    !face.Parse(truncated), "and a font cut short is refused rather than read");
            }
        }
        return passed;
    }
}

bool RunFontFaceTests()
{
    bool passed = CheckFont(
        "D2Coding-Ver1.3.3-20260725.ttf",
        GameEngine::Text::FontFace::OutlineFormat::TrueType, true);
    passed &= CheckFont(
        "MaruBuri-Regular.otf",
        GameEngine::Text::FontFace::OutlineFormat::CompactFontFormat, true);
    passed &= CheckFont(
        "NanumSquareNeoOTF-Rg.otf",
        GameEngine::Text::FontFace::OutlineFormat::CompactFontFormat, true);
    passed &= CheckRubbishIsRefused();
    return passed;
}

static const TestSupport::Registration gFontFaceTests{
    "RenderCache", "font face tests should pass", RunFontFaceTests };
