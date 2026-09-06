#include "TrueTypeBoundaryTests.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "Text/FontFace.h"
#include "Text/TrueTypeOutlines.h"
#include "TestSupport.h"

namespace
{
    using Bytes = std::vector<std::byte>;

    void Put16(Bytes& bytes, const std::size_t offset, const unsigned int value)
    {
        bytes[offset] = static_cast<std::byte>((value >> 8) & 0xff);
        bytes[offset + 1] = static_cast<std::byte>(value & 0xff);
    }

    void Put32(Bytes& bytes, const std::size_t offset, const std::size_t value)
    {
        for (std::size_t index = 0; index < 4; ++index)
        {
            bytes[offset + index] = static_cast<std::byte>((value >> ((3 - index) * 8)) & 0xff);
        }
    }

    // 외부 폰트 설치 여부와 관계없이 파서 경계를 시험하는 한 글리프짜리 sfnt다.
    Bytes MakeFont(Bytes glyph)
    {
        if (glyph.size() % 2 != 0)
        {
            glyph.push_back(std::byte{});
        }
        std::array<std::pair<std::string_view, Bytes>, 7> tables{
            std::pair{ "head", Bytes(54) }, { "maxp", Bytes(6) },
            { "hhea", Bytes(36) }, { "hmtx", Bytes(4) },
            { "cmap", Bytes(36) }, { "loca", Bytes(4) }, { "glyf", std::move(glyph) }
        };
        Put16(tables[0].second, 18, 1000);
        Put16(tables[1].second, 4, 1);
        Put16(tables[2].second, 34, 1);
        Put16(tables[3].second, 0, 500);
        Bytes& cmap = tables[4].second;
        Put16(cmap, 2, 1);
        Put16(cmap, 4, 3);
        Put16(cmap, 6, 1);
        Put32(cmap, 8, 12);
        Put16(cmap, 12, 4);
        Put16(cmap, 14, 24);
        Put16(cmap, 18, 2);
        Put16(cmap, 26, 0xffff);
        Put16(cmap, 30, 0xffff);
        Put16(cmap, 32, 1);
        Put16(tables[5].second, 2, static_cast<unsigned int>(tables[6].second.size() / 2));

        Bytes font(12 + tables.size() * 16);
        Put32(font, 0, 0x00010000);
        Put16(font, 4, static_cast<unsigned int>(tables.size()));
        for (std::size_t index = 0; index < tables.size(); ++index)
        {
            const auto& [tag, payload] = tables[index];
            const std::size_t record = 12 + index * 16;
            for (std::size_t character = 0; character < 4; ++character)
            {
                font[record + character] = static_cast<std::byte>(tag[character]);
            }
            Put32(font, record + 8, font.size());
            Put32(font, record + 12, payload.size());
            font.insert(font.end(), payload.begin(), payload.end());
        }
        return font;
    }

    bool CheckGlyph(Bytes glyph, const bool expected)
    {
        GameEngine::Text::FontFace face;
        if (!TestSupport::Expect(face.Parse(MakeFont(std::move(glyph))), "the boundary font parses"))
        {
            return false;
        }
        GameEngine::Text::GlyphOutline outline;
        return TestSupport::Expect(
            GameEngine::Text::GetTrueTypeGlyphOutline(face, 0, outline) == expected,
            "empty glyphs succeed and malformed glyph records are refused") &&
            TestSupport::Expect(!expected || outline.IsEmpty(), "a zero-contour glyph has no ink");
    }
}

bool RunTrueTypeBoundaryTests()
{
    bool passed = CheckGlyph(Bytes(10), true);

    Bytes repeatedFlags(16);
    Put16(repeatedFlags, 0, 1);
    // One point, but the flag run claims two.
    repeatedFlags[14] = std::byte{ 0x39 };
    repeatedFlags[15] = std::byte{ 1 };
    passed &= CheckGlyph(std::move(repeatedFlags), false);

    Bytes unorderedContours(18);
    Put16(unorderedContours, 0, 2);
    Put16(unorderedContours, 10, 1);
    Put16(unorderedContours, 12, 0);
    unorderedContours[16] = std::byte{ 0x31 };
    passed &= CheckGlyph(std::move(unorderedContours), false);

    Bytes truncatedInstructions(14);
    Put16(truncatedInstructions, 0, 1);
    Put16(truncatedInstructions, 12, 100);
    passed &= CheckGlyph(std::move(truncatedInstructions), false);
    return passed;
}

static const TestSupport::Registration gTrueTypeBoundaryTests{
    "RenderCache", "TrueType empty and malformed contours are handled safely", RunTrueTypeBoundaryTests };
