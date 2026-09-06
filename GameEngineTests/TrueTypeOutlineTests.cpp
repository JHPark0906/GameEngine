#include "TrueTypeOutlineTests.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Text/FontFace.h"
#include "Text/TrueTypeOutlines.h"
#include "TestSupport.h"

using TestSupport::Expect;

/// <summary>
/// glyf에서 읽어 낸 윤곽선을 그 폰트가 말하는 것과 견준다.
///
/// 여기서 묻는 것은 「그럴듯한 점들이 나왔는가」가 아니다 — 그건 무엇이 나와도 통과한다.
/// 폰트 파일은 글리프마다 <b>자기 경계 상자를 따로 적어 둔다</b>. 그 값은 우리가 계산하지 않은
/// 제2의 진실이므로, 우리가 읽은 점들이 그 상자와 맞는지 물으면 「읽기가 맞았는가」를 폰트
/// 자신에게 확인받는 셈이 된다. 좌표 델타 누적이나 반복 플래그 처리가 어긋나면 상자가 어긋난다.
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

    struct Bounds
    {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        bool any = false;
    };

    /// <summary>
    /// 윤곽선이 실제로 차지하는 자리다.
    ///
    /// 제어점까지 함께 넣는다. 3차 곡선은 제어점 밖으로 나가지 않으므로 이 상자는 곡선을 반드시
    /// 품고, 폰트가 적어 둔 상자보다 조금 클 수는 있어도 작을 수는 없다.
    /// </summary>
    [[nodiscard]] Bounds MeasureOutline(const GameEngine::Text::GlyphOutline& outline)
    {
        Bounds bounds;
        const auto include = [&bounds](const GameEngine::Text::OutlinePoint point)
        {
            if (!bounds.any)
            {
                bounds = Bounds{ point.x, point.y, point.x, point.y, true };
                return;
            }
            bounds.minX = (std::min)(bounds.minX, point.x);
            bounds.minY = (std::min)(bounds.minY, point.y);
            bounds.maxX = (std::max)(bounds.maxX, point.x);
            bounds.maxY = (std::max)(bounds.maxY, point.y);
        };
        for (const GameEngine::Text::OutlineContour& contour : outline.contours)
        {
            include(contour.start);
            for (const GameEngine::Text::OutlineSegment& segment : contour.segments)
            {
                include(segment.control1);
                include(segment.control2);
                include(segment.end);
            }
        }
        return bounds;
    }

    /// <summary>글리프가 스스로 적어 둔 경계 상자다. glyf 항목의 첫 열 바이트에 있다.</summary>
    [[nodiscard]] bool DeclaredBounds(
        const GameEngine::Text::FontFace& face, const std::uint16_t glyphId, Bounds& bounds)
    {
        const std::span<const std::byte> head = face.GetTable("head");
        const std::span<const std::byte> loca = face.GetTable("loca");
        const std::span<const std::byte> glyf = face.GetTable("glyf");
        const auto readU16 = [](const std::span<const std::byte> bytes, const std::size_t offset)
        {
            return static_cast<std::uint16_t>(
                (static_cast<unsigned int>(bytes[offset]) << 8) |
                static_cast<unsigned int>(bytes[offset + 1]));
        };
        const auto readS16 = [&readU16](
                                 const std::span<const std::byte> bytes, const std::size_t offset)
        {
            return static_cast<std::int16_t>(readU16(bytes, offset));
        };

        const bool longLoca = readS16(head, 50) == 1;
        std::size_t start = 0;
        std::size_t end = 0;
        if (longLoca)
        {
            const auto read32 = [](const std::span<const std::byte> bytes, const std::size_t offset)
            {
                return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
                    (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
                    (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
                    static_cast<std::uint32_t>(bytes[offset + 3]);
            };
            start = read32(loca, static_cast<std::size_t>(glyphId) * 4);
            end = read32(loca, static_cast<std::size_t>(glyphId) * 4 + 4);
        }
        else
        {
            start = static_cast<std::size_t>(readU16(loca, glyphId * 2u)) * 2;
            end = static_cast<std::size_t>(readU16(loca, glyphId * 2u + 2u)) * 2;
        }
        if (end <= start || end > glyf.size())
        {
            return false;
        }
        bounds.minX = static_cast<float>(readS16(glyf, start + 2));
        bounds.minY = static_cast<float>(readS16(glyf, start + 4));
        bounds.maxX = static_cast<float>(readS16(glyf, start + 6));
        bounds.maxY = static_cast<float>(readS16(glyf, start + 8));
        bounds.any = true;
        return true;
    }

    /// <summary>글리프 하나를 읽고, 그 폰트가 적어 둔 상자와 견준다.</summary>
    [[nodiscard]] bool CheckGlyph(
        const GameEngine::Text::FontFace& face,
        const char32_t codePoint,
        const char* const what,
        const bool expectComposite)
    {
        const std::uint16_t glyphId = face.GetGlyphIndex(codePoint);
        if (!Expect(glyphId != 0, "the character has a glyph"))
        {
            return false;
        }

        GameEngine::Text::GlyphOutline outline;
        if (!Expect(
                GetTrueTypeGlyphOutline(face, glyphId, outline),
                "the glyph's outline can be read"))
        {
            return false;
        }

        bool passed = Expect(!outline.IsEmpty(), "and it has contours");
        if (outline.IsEmpty())
        {
            return false;
        }

        const Bounds measured = MeasureOutline(outline);
        Bounds declared;
        if (!Expect(DeclaredBounds(face, glyphId, declared), "and the font declares its bounds"))
        {
            return false;
        }

        std::size_t segments = 0;
        for (const GameEngine::Text::OutlineContour& contour : outline.contours)
        {
            segments += contour.segments.size();
        }
        std::cout << "  " << what << " glyph " << glyphId << ": " << outline.contours.size()
                  << " contour(s), " << segments << " segment(s), read ["
                  << measured.minX << "," << measured.minY << ".." << measured.maxX << ","
                  << measured.maxY << "] against declared [" << declared.minX << ","
                  << declared.minY << ".." << declared.maxX << "," << declared.maxY << "]\n";

        // 읽은 상자는 선언된 상자를 품어야 한다. 제어점이 곡선 밖으로 나가므로 조금 클 수는
        // 있지만, 작으면 점을 잃은 것이다. 여유는 한 em의 100분의 1로 둔다.
        const float tolerance = static_cast<float>(face.GetUnitsPerEm()) / 100.0f;
        const bool contains = measured.minX <= declared.minX + tolerance &&
            measured.minY <= declared.minY + tolerance &&
            measured.maxX >= declared.maxX - tolerance &&
            measured.maxY >= declared.maxY - tolerance;
        if (!contains)
        {
            std::cerr << "  " << what << ": the outline that was read does not cover the box the "
                      << "font declares, so points were lost or misplaced\n";
        }
        passed &= Expect(contains, "and the outline covers the box the font declares");

        // 그리고 터무니없이 크지도 않아야 한다. 델타 누적이 어긋나면 점이 멀리 날아가는데,
        // 그것은 위 검사만으로는 통과한다.
        const float slack = static_cast<float>(face.GetUnitsPerEm());
        const bool sane = measured.minX >= declared.minX - slack &&
            measured.maxX <= declared.maxX + slack &&
            measured.minY >= declared.minY - slack &&
            measured.maxY <= declared.maxY + slack;
        passed &= Expect(sane, "and does not run far outside it");

        if (expectComposite)
        {
            // 합성 글리프는 부품을 여럿 붙이므로 고리가 하나로 끝나지 않는다. 한글 음절이
            // 자모로 조립돼 있다는 사실이 여기서 눈에 보인다.
            passed &= Expect(
                outline.contours.size() >= 2,
                "and a composite glyph brings in more than one component's contours");
        }
        return passed;
    }
}

bool RunTrueTypeOutlineTests()
{
    const std::filesystem::path path = FontDirectory() / "D2Coding-Ver1.3.3-20260725.ttf";
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
    {
        std::cout << "  truetype outline tests skipped: D2Coding is not in this checkout\n";
        return true;
    }

    GameEngine::Text::FontFace face;
    if (!Expect(face.Parse(ReadBytes(path)), "the TrueType font parses"))
    {
        return false;
    }

    // 'A'는 단순 글리프이고, 완성형 한글은 자모 부품을 붙인 합성 글리프다 — 이 폰트의 35%가
    // 그 형태다. 둘 다 확인해야 「영문은 나오는데 한글이 안 나온다」가 걸린다.
    bool passed = CheckGlyph(face, U'A', "latin 'A'", false);
    passed &= CheckGlyph(face, U'가', "hangul 'ga'", true);
    passed &= CheckGlyph(face, U'힣', "hangul 'hih'", true);

    // 공백은 잉크가 없다. 윤곽선이 비는 것은 실패가 아니라 답이며, 이것을 실패로 다루면 모든
    // 문장이 첫 칸에서 멈춘다.
    GameEngine::Text::GlyphOutline space;
    const std::uint16_t spaceGlyph = face.GetGlyphIndex(U' ');
    passed &= Expect(
        GetTrueTypeGlyphOutline(face, spaceGlyph, space) && space.IsEmpty(),
        "a space reads as an empty outline rather than a failure");

    // 없는 글리프 번호를 물으면 거절한다. 여기서 배열 밖으로 나가면 폰트 파일 하나가 프로세스를
    // 죽일 수 있다.
    GameEngine::Text::GlyphOutline beyond;
    passed &= Expect(
        !GetTrueTypeGlyphOutline(
            face, static_cast<std::uint16_t>(face.GetGlyphCount() > 0xFFFEu ? 0xFFFFu : 0xFFFFu),
            beyond),
        "and a glyph number the font does not have is refused");
    return passed;
}

static const TestSupport::Registration gTrueTypeOutlineTests{
    "RenderCache", "truetype outline tests should pass", RunTrueTypeOutlineTests };
