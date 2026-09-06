#include "GlyphRasterizerTests.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Text/FontFace.h"
#include "Text/GlyphRasterizer.h"
#include "Text/TrueTypeOutlines.h"
#include "TestSupport.h"

using TestSupport::Expect;

/// <summary>
/// 윤곽선을 픽셀로 바꾸는 자리를 묻는다.
///
/// 커버리지의 「옳음」은 단언하기 어렵다 — 무엇과 견줄 것인가가 곧 이 단계의 미지이기 때문이다.
/// 그래서 여기서는 <b>스스로 참이어야 하는 것들</b>만 묻는다: 사각형은 넓이가 정확히 계산되고,
/// 글자에는 안과 밖이 있고, 두 배로 그리면 네 배로 칠해지고, 구멍 있는 글자는 구멍이 비어야
/// 한다. 다른 래스터라이저와 견주는 일은 이 파일의 몫이 아니다 — 이 자리는 이쪽이 스스로
/// 모순되지 않는지만 확인한다.
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

    /// <summary>커버리지의 총합이다. 잉크의 넓이를 픽셀 단위로 잰 값이 된다.</summary>
    [[nodiscard]] double TotalCoverage(const GameEngine::Text::CoverageBitmap& bitmap)
    {
        double total = 0.0;
        for (const std::byte value : bitmap.alpha)
        {
            total += static_cast<double>(static_cast<unsigned char>(value)) / 255.0;
        }
        return total;
    }

    /// <summary>축에 나란한 사각형 하나짜리 윤곽선이다. 넓이를 손으로 알 수 있는 유일한 도형이다.</summary>
    [[nodiscard]] GameEngine::Text::GlyphOutline MakeRectangle(
        const float left, const float bottom, const float right, const float top)
    {
        using namespace GameEngine::Text;
        OutlineContour contour;
        contour.start = OutlinePoint{ left, bottom };
        contour.segments.push_back(PromoteLine({ left, bottom }, { right, bottom }));
        contour.segments.push_back(PromoteLine({ right, bottom }, { right, top }));
        contour.segments.push_back(PromoteLine({ right, top }, { left, top }));
        contour.segments.push_back(PromoteLine({ left, top }, { left, bottom }));
        GlyphOutline outline;
        outline.contours.push_back(std::move(contour));
        return outline;
    }

    /// <summary>
    /// 넓이를 아는 도형으로 커버리지가 맞는지 본다.
    ///
    /// 100x50 사각형은 어떤 배율에서든 넓이가 정확히 <c>100*50*배율²</c>이다. 커버리지 총합이
    /// 그것과 다르면 안티에일리어싱이 잉크를 만들어 내거나 잃고 있다는 뜻이고, 그 오차는 글자
    /// 전체에 고르게 퍼져 눈으로는 원인을 짚어내기 어렵다.
    /// </summary>
    [[nodiscard]] bool CheckAreaIsExact(const GameEngine::Text::ScanConversion method,
                                        const char* const name)
    {
        using namespace GameEngine::Text;
        const GlyphOutline rectangle = MakeRectangle(0.0f, 0.0f, 100.0f, 50.0f);

        bool passed = true;
        for (const float pixelsPerUnit : { 0.05f, 0.25f, 1.0f })
        {
            RasterizerSettings settings;
            settings.method = method;
            CoverageBitmap bitmap;
            if (!Expect(
                    RasterizeGlyphOutline(rectangle, pixelsPerUnit, settings, bitmap),
                    "a rectangle rasterizes"))
            {
                return false;
            }
            const double expected =
                100.0 * 50.0 * static_cast<double>(pixelsPerUnit) * pixelsPerUnit;
            const double measured = TotalCoverage(bitmap);
            const double error = std::abs(measured - expected) / expected;
            std::cout << "  " << name << " area at " << pixelsPerUnit << " px/unit: expected "
                      << expected << ", measured " << measured << " (" << (error * 100.0)
                      << "% off), bitmap " << bitmap.width << "x" << bitmap.height << "\n";
            // 3%는 표본 격자가 만드는 오차의 여유다. 초과표본 방식은 가로도 표본이라 더 크다.
            passed &= Expect(error < 0.03, "and its total coverage is the rectangle's area");
        }
        return passed;
    }

    /// <summary>배율을 두 배로 하면 잉크가 네 배가 된다. 넓이는 길이의 제곱이므로.</summary>
    [[nodiscard]] bool CheckScalesWithArea(const GameEngine::Text::FontFace& face)
    {
        using namespace GameEngine::Text;
        GlyphOutline outline;
        if (!Expect(
                GetTrueTypeGlyphOutline(face, face.GetGlyphIndex(U'A'), outline),
                "the glyph reads"))
        {
            return false;
        }

        const float small = 16.0f / static_cast<float>(face.GetUnitsPerEm());
        RasterizerSettings settings;
        CoverageBitmap atSmall;
        CoverageBitmap atLarge;
        if (!RasterizeGlyphOutline(outline, small, settings, atSmall) ||
            !RasterizeGlyphOutline(outline, small * 2.0f, settings, atLarge))
        {
            return Expect(false, "both sizes rasterize");
        }

        const double ratio = TotalCoverage(atLarge) / TotalCoverage(atSmall);
        std::cout << "  'A' ink at 16px = " << TotalCoverage(atSmall) << ", at 32px = "
                  << TotalCoverage(atLarge) << ", ratio " << ratio << " (4 expected)\n";
        // 작은 크기에서는 안티에일리어싱이 획 하나를 여러 픽셀에 나눠 담아 비율이 흔들린다.
        return Expect(ratio > 3.5 && ratio < 4.5, "twice the size covers four times the area");
    }

    /// <summary>
    /// 구멍이 있는 글자는 구멍이 비어야 한다.
    ///
    /// 이것이 0이 아닌 감김수 규칙을 묻는 자리다. 홀짝 규칙을 썼거나 고리의 감김 방향을 잃으면
    /// 구멍이 메워지거나 반대로 획이 뚫린다.
    /// </summary>
    [[nodiscard]] bool CheckCounterIsHollow(const GameEngine::Text::FontFace& face)
    {
        using namespace GameEngine::Text;
        GlyphOutline outline;
        // 'O'는 바깥 고리와 안쪽 고리가 반대로 감긴 가장 단순한 글자다.
        if (!GetTrueTypeGlyphOutline(face, face.GetGlyphIndex(U'O'), outline))
        {
            return Expect(false, "the letter O reads");
        }

        RasterizerSettings settings;
        CoverageBitmap bitmap;
        if (!RasterizeGlyphOutline(
                outline, 64.0f / static_cast<float>(face.GetUnitsPerEm()), settings, bitmap) ||
            bitmap.IsEmpty())
        {
            return Expect(false, "the letter O rasterizes");
        }

        const std::size_t centre = static_cast<std::size_t>(bitmap.height / 2) * bitmap.width +
            bitmap.width / 2;
        const auto middle = static_cast<unsigned char>(bitmap.alpha[centre]);
        // 획 위의 한 점: 세로 중앙에서 왼쪽 가장자리를 향해 걸어가다 처음 만나는 잉크다.
        unsigned char stroke = 0;
        for (unsigned int x = 0; x < bitmap.width / 2; ++x)
        {
            const auto value = static_cast<unsigned char>(
                bitmap.alpha[static_cast<std::size_t>(bitmap.height / 2) * bitmap.width + x]);
            stroke = (std::max)(stroke, value);
        }
        std::cout << "  'O' " << bitmap.width << "x" << bitmap.height << ": centre alpha "
                  << static_cast<unsigned int>(middle) << ", stroke alpha "
                  << static_cast<unsigned int>(stroke) << "\n";

        bool passed = Expect(stroke > 200, "the letter's stroke is inked");
        passed &= Expect(middle < 32, "and the hole inside it is not");
        return passed;
    }

    /// <summary>두 방식이 같은 글자에 대해 무엇을 내는지 나란히 잰다.</summary>
    [[nodiscard]] bool CompareMethods(const GameEngine::Text::FontFace& face)
    {
        using namespace GameEngine::Text;
        GlyphOutline outline;
        if (!GetTrueTypeGlyphOutline(face, face.GetGlyphIndex(U'가'), outline))
        {
            return Expect(false, "the syllable reads");
        }

        bool passed = true;
        for (const float size : { 11.0f, 16.0f, 32.0f })
        {
            const float pixelsPerUnit = size / static_cast<float>(face.GetUnitsPerEm());
            CoverageBitmap analytic;
            CoverageBitmap sampled;
            RasterizerSettings analyticSettings;
            analyticSettings.method = ScanConversion::AnalyticHorizontal;
            RasterizerSettings sampledSettings;
            sampledSettings.method = ScanConversion::Supersampled;

            const auto started = std::chrono::steady_clock::now();
            const bool builtAnalytic =
                RasterizeGlyphOutline(outline, pixelsPerUnit, analyticSettings, analytic);
            const auto middle = std::chrono::steady_clock::now();
            const bool builtSampled =
                RasterizeGlyphOutline(outline, pixelsPerUnit, sampledSettings, sampled);
            const auto finished = std::chrono::steady_clock::now();
            if (!builtAnalytic || !builtSampled)
            {
                return Expect(false, "both methods rasterize");
            }

            double difference = 0.0;
            const std::size_t count = (std::min)(analytic.alpha.size(), sampled.alpha.size());
            for (std::size_t index = 0; index < count; ++index)
            {
                difference += std::abs(
                    static_cast<double>(static_cast<unsigned char>(analytic.alpha[index])) -
                    static_cast<double>(static_cast<unsigned char>(sampled.alpha[index])));
            }
            const double meanDifference = count > 0 ? difference / static_cast<double>(count) : 0.0;
            const auto analyticMicros =
                std::chrono::duration_cast<std::chrono::microseconds>(middle - started).count();
            const auto sampledMicros =
                std::chrono::duration_cast<std::chrono::microseconds>(finished - middle).count();

            std::cout << "  method comparison at " << size << "px: mean |diff| " << meanDifference
                      << "/255, analytic " << analyticMicros << "us, supersampled "
                      << sampledMicros << "us, ink " << TotalCoverage(analytic) << " vs "
                      << TotalCoverage(sampled) << "\n";
            passed &= Expect(
                analytic.width == sampled.width && analytic.height == sampled.height,
                "the two methods agree on the bitmap's size");
        }
        return passed;
    }
}

bool RunGlyphRasterizerTests()
{
    using namespace GameEngine::Text;

    bool passed = CheckAreaIsExact(ScanConversion::AnalyticHorizontal, "analytic");
    passed &= CheckAreaIsExact(ScanConversion::Supersampled, "supersampled");

    // 잉크 없는 윤곽선은 크기 0의 성공이다.
    CoverageBitmap empty;
    RasterizerSettings settings;
    passed &= Expect(
        RasterizeGlyphOutline(GlyphOutline{}, 1.0f, settings, empty) && empty.IsEmpty(),
        "an outline with no contours rasterizes to nothing, and that is success");

    const std::filesystem::path path = FontDirectory() / "D2Coding-Ver1.3.3-20260725.ttf";
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
    {
        std::cout << "  glyph rasterizer tests: D2Coding is not in this checkout\n";
        return passed;
    }
    FontFace face;
    if (!Expect(face.Parse(ReadBytes(path)), "the font parses"))
    {
        return false;
    }

    passed &= CheckScalesWithArea(face);
    passed &= CheckCounterIsHollow(face);
    passed &= CompareMethods(face);
    return passed;
}

static const TestSupport::Registration gGlyphRasterizerTests{
    "RenderCache", "glyph rasterizer tests should pass", RunGlyphRasterizerTests };
