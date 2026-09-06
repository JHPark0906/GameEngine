#include "EngineTextRasterizerTests.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Text/EngineTextRasterizer.h"
#include "TestSupport.h"

using TestSupport::Expect;

/// <summary>
/// 조립된 배치를 계약 쪽에서 묻는다.
///
/// 조각들은 저마다 이미 시험이 있다 — 어느 폰트로 그릴지, 어디서 줄을 바꿀지, 글자 모양이
/// 어떤지. 여기서 묻는 것은 그것들을 엮은 결과가 <c>ITextRasterizer</c>가 약속한 것과 맞는가다:
/// 글리프가 <b>어디에 놓이는가</b>, 줄이 <b>어떻게 쌓이는가</b>, 정렬이 <b>무엇을 미는가</b>.
/// </summary>
namespace
{
    using namespace GameEngine;

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

    [[nodiscard]] std::size_t CountGlyphs(const Platform::TextGlyphLayout& layout)
    {
        std::size_t total = 0;
        for (const Platform::PositionedGlyphRun& run : layout.runs)
        {
            total += run.glyphs.size();
        }
        return total;
    }

    /// <summary>몇 개의 다른 y가 나왔는가. 곧 줄 수다.</summary>
    [[nodiscard]] std::vector<float> DistinctBaselines(const Platform::TextGlyphLayout& layout)
    {
        std::vector<float> baselines;
        for (const Platform::PositionedGlyphRun& run : layout.runs)
        {
            for (const Platform::PositionedGlyph& glyph : run.glyphs)
            {
                bool seen = false;
                for (const float existing : baselines)
                {
                    if (std::abs(existing - glyph.y) < 0.01f)
                    {
                        seen = true;
                        break;
                    }
                }
                if (!seen)
                {
                    baselines.push_back(glyph.y);
                }
            }
        }
        return baselines;
    }

    [[nodiscard]] float FirstX(const Platform::TextGlyphLayout& layout)
    {
        for (const Platform::PositionedGlyphRun& run : layout.runs)
        {
            if (!run.glyphs.empty())
            {
                return run.glyphs.front().x;
            }
        }
        return 0.0f;
    }
}

bool RunEngineTextRasterizerTests()
{
    const std::filesystem::path coding = FontDirectory() / "D2Coding-Ver1.3.3-20260725.ttf";
    std::error_code error;
    if (!std::filesystem::is_regular_file(coding, error) || error)
    {
        std::cout << "  engine text rasterizer tests skipped: D2Coding is not in this checkout\n";
        return true;
    }

    Text::EngineTextRasterizer rasterizer;
    bool passed = Expect(rasterizer.Initialize(), "the rasterizer initializes");

    // 폰트가 하나도 없을 때. 배치는 성공한 척하지 않는다 — 그러면 글자가 없는 화면이 정상으로
    // 보고된다.
    {
        Platform::TextRasterizationRequest request;
        request.text = "A";
        request.fontSize = 16.0f;
        Platform::TextGlyphLayout layout;
        passed &= Expect(
            !rasterizer.LayoutText(request, layout),
            "laying out text with no font registered fails rather than reporting an empty line");
        passed &= Expect(
            !rasterizer.IsSupported(request),
            "and the request is reported as unsupported while there is no font");
    }

    passed &= Expect(
        rasterizer.RegisterFont("ui", ReadBytes(coding)), "a font registers");

    // 빈 글자는 성공이고 결과가 비어 있다. 실패로 다루면 빈 라벨이 오류가 된다.
    {
        Platform::TextRasterizationRequest request;
        request.fontFamily = "ui";
        request.fontSize = 16.0f;
        Platform::TextGlyphLayout layout;
        passed &= Expect(
            rasterizer.LayoutText(request, layout) && layout.runs.empty(),
            "empty text lays out to nothing, and that is success");
    }

    // 한 줄. 글리프가 글자 수만큼 나오고, 진행폭만큼 오른쪽으로 나아간다.
    {
        Platform::TextRasterizationRequest request;
        request.text = "AB";
        request.fontFamily = "ui";
        request.fontSize = 16.0f;
        Platform::TextGlyphLayout layout;
        if (!Expect(rasterizer.LayoutText(request, layout), "a short string lays out"))
        {
            return false;
        }
        passed &= Expect(CountGlyphs(layout) == 2, "with one glyph per character");
        const Platform::PositionedGlyphRun& run = layout.runs.front();
        std::cout << "  one line: glyphs at x " << run.glyphs[0].x << " and " << run.glyphs[1].x
                  << ", y " << run.glyphs[0].y << ", block " << layout.width << "x"
                  << layout.height << "\n";
        passed &= Expect(
            run.glyphs[0].x == 0.0f && run.glyphs[1].x > run.glyphs[0].x,
            "the pen starts at the left edge and advances");
        passed &= Expect(
            std::abs(run.glyphs[0].y - run.glyphs[1].y) < 0.01f,
            "and both sit on the same baseline");
        passed &= Expect(
            run.glyphs[0].y > 0.0f,
            "which is below the top of the block, by the ascender");
    }

    // 줄바꿈 문자. 두 줄이 되고 아래 줄이 더 아래에 앉는다.
    {
        Platform::TextRasterizationRequest request;
        request.text = "A\nB";
        request.fontFamily = "ui";
        request.fontSize = 16.0f;
        Platform::TextGlyphLayout layout;
        if (!Expect(rasterizer.LayoutText(request, layout), "text with a newline lays out"))
        {
            return false;
        }
        const std::vector<float> baselines = DistinctBaselines(layout);
        std::cout << "  two lines: baselines";
        for (const float baseline : baselines)
        {
            std::cout << " " << baseline;
        }
        std::cout << ", block height " << layout.height << "\n";
        passed &= Expect(baselines.size() == 2, "producing two baselines");
        if (baselines.size() == 2)
        {
            passed &= Expect(
                baselines[1] > baselines[0], "with the second line below the first");
        }
        passed &= Expect(CountGlyphs(layout) == 2, "and the newline itself draws nothing");
    }

    // 정렬. 같은 글자를 같은 폭에 세 가지로 놓고 시작 x를 견준다.
    {
        Platform::TextRasterizationRequest request;
        request.text = "A";
        request.fontFamily = "ui";
        request.fontSize = 16.0f;
        request.maxWidth = 200.0f;

        // 줄의 실제 폭은 짐작하지 말고 재서 가져온다 — 폭을 주지 않은 배치의 블록 폭이 곧
        // 그것이다. 정렬이 얼마를 밀어야 맞는지는 그 값으로만 말할 수 있다.
        Platform::TextGlyphLayout unbounded;
        Platform::TextRasterizationRequest measure = request;
        measure.maxWidth = 0.0f;
        if (!Expect(rasterizer.LayoutText(measure, unbounded), "the line measures on its own"))
        {
            return false;
        }
        const auto lineWidth = static_cast<float>(unbounded.width);

        Platform::TextGlyphLayout left;
        Platform::TextGlyphLayout centre;
        Platform::TextGlyphLayout right;
        request.alignment = Platform::TextAlignment::Left;
        const bool laidLeft = rasterizer.LayoutText(request, left);
        request.alignment = Platform::TextAlignment::Center;
        const bool laidCentre = rasterizer.LayoutText(request, centre);
        request.alignment = Platform::TextAlignment::Right;
        const bool laidRight = rasterizer.LayoutText(request, right);
        if (!Expect(laidLeft && laidCentre && laidRight, "all three alignments lay out"))
        {
            return false;
        }
        std::cout << "  alignment of a " << lineWidth << "px line in a 200px block: left "
                  << FirstX(left) << ", centre " << FirstX(centre) << ", right " << FirstX(right)
                  << "\n";
        passed &= Expect(FirstX(left) == 0.0f, "left alignment starts at the left edge");
        passed &= Expect(
            FirstX(centre) > FirstX(left) && FirstX(right) > FirstX(centre),
            "and centre sits between left and right");
        // 오른쪽 정렬은 글자의 오른끝이 블록의 오른끝에 닿아야 한다.
        passed &= Expect(
            std::abs(FirstX(right) + lineWidth - 200.0f) <= 1.0f,
            "and right alignment puts the line's end at the block's edge");
        // 가운데 정렬은 양옆에 같은 여백을 남긴다.
        passed &= Expect(
            std::abs(FirstX(centre) - (200.0f - lineWidth - FirstX(centre))) <= 1.0f,
            "and centre alignment leaves the same margin on both sides");
    }

    // 래스터화. 배치가 답한 fontKey와 글리프로 그림이 나온다.
    {
        Platform::TextRasterizationRequest request;
        request.text = "A";
        request.fontFamily = "ui";
        request.fontSize = 32.0f;
        Platform::TextGlyphLayout layout;
        if (!Expect(rasterizer.LayoutText(request, layout), "a glyph lays out for rasterizing"))
        {
            return false;
        }
        const Platform::PositionedGlyphRun& run = layout.runs.front();
        Platform::RasterizedGlyph glyph;
        passed &= Expect(
            rasterizer.RasterizeGlyph(run.fontKey, request.fontSize, run.glyphs[0].glyphId, glyph),
            "and the key the layout reported finds the same face again");
        passed &= Expect(
            glyph.width > 0 && glyph.height > 0 && !glyph.alphaPixels.empty(),
            "and produces coverage");

        // 모르는 fontKey는 거절이다. 여기서 무언가를 그려 주면 배치와 래스터화가 서로 다른
        // face를 쓰고도 조용히 넘어간다.
        Platform::RasterizedGlyph unknown;
        passed &= Expect(
            !rasterizer.RasterizeGlyph(9999, request.fontSize, 1, unknown),
            "a font key that names no face is refused");
    }

    // 🔴 두부가 배치를 지나 래스터화까지 살아 나오는가. 등록 폰트에 없는 글자다.
    {
        Platform::TextRasterizationRequest request;
        // U+E000은 사용자 정의 영역이라 어떤 폰트도 채워 둘 이유가 없다.
        request.text = "\xEE\x80\x80";
        request.fontFamily = "ui";
        request.fontSize = 32.0f;
        Platform::TextGlyphLayout layout;
        if (!Expect(rasterizer.LayoutText(request, layout), "a character no font has lays out"))
        {
            return false;
        }
        passed &= Expect(CountGlyphs(layout) == 1, "as one glyph");
        const Platform::PositionedGlyphRun& run = layout.runs.front();
        Platform::RasterizedGlyph glyph;
        if (!Expect(
                rasterizer.RasterizeGlyph(
                    run.fontKey, request.fontSize, run.glyphs[0].glyphId, glyph),
                "and rasterizes"))
        {
            return false;
        }
        bool hasInk = false;
        for (const std::byte value : glyph.alphaPixels)
        {
            if (static_cast<unsigned char>(value) > 0)
            {
                hasInk = true;
                break;
            }
        }
        std::cout << "  tofu through layout: glyph " << run.glyphs[0].glyphId << ", "
                  << glyph.width << "x" << glyph.height << ", ink " << (hasInk ? "yes" : "no")
                  << "\n";
        // 조각 단위로는 이미 확인했지만, 배치를 지나면서 잃지 않는지는 여기서만 확인된다.
        passed &= Expect(hasInk, "and still draws a visible box after going through layout");
    }

    // 줄바꿈 폭. 폭을 좁히면 줄이 늘어난다.
    {
        Platform::TextRasterizationRequest request;
        request.text = "가나다라마바사아자차";
        request.fontFamily = "ui";
        request.fontSize = 16.0f;
        request.maxWidth = 60.0f;
        Platform::TextGlyphLayout layout;
        if (!Expect(rasterizer.LayoutText(request, layout), "Korean text wraps"))
        {
            return false;
        }
        const std::vector<float> baselines = DistinctBaselines(layout);
        std::cout << "  wrapped Korean: " << CountGlyphs(layout) << " glyphs over "
                  << baselines.size() << " line(s), block " << layout.width << "x"
                  << layout.height << "\n";
        passed &= Expect(CountGlyphs(layout) == 10, "keeping every character");
        passed &= Expect(baselines.size() > 1, "and using more than one line");
    }
    return passed;
}

static const TestSupport::Registration gEngineTextRasterizerTests{
    "RenderCache", "engine text rasterizer tests should pass", RunEngineTextRasterizerTests };
