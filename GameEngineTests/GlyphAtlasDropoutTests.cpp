#include "GlyphAtlasDropoutTests.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>

#include "Platform/ITextRasterizer.h"
#include "Platform/PlatformServices.h"
#include "Rendering/GlyphAtlas.h"
#include "Rendering/TextRasterizationCache.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>
    /// 글리프마다 자기 번호를 픽셀 값으로 칠하는 래스터라이저다. 값이 곧 정체성이므로, 페이지의
    /// 어느 사각형을 읽으면 그 자리에 어느 글리프가 놓였는지 답이 나온다 — 이 시험이 슬롯의
    /// 존재가 아니라 좌표의 정확성을 잴 수 있는 이유다.
    ///
    /// 크기 100 이상은 한 페이지에 하나만 들어가는 글리프를 낸다. 페이지 상한을 몇 줄로 태워
    /// 아틀라스가 처음부터 다시 시작하게 만드는 손잡이다.
    /// </summary>
    class MarkingRasterizer final : public GameEngine::Platform::ITextRasterizer
    {
    public:
        [[nodiscard]] static std::byte MarkerFor(const std::uint16_t glyphId)
        {
            return static_cast<std::byte>(1 + glyphId % 254);
        }

        [[nodiscard]] bool Initialize() override { return true; }

        [[nodiscard]] bool RegisterFont(
            const std::string_view, const std::span<const std::byte>) override
        {
            return true;
        }

        [[nodiscard]] bool IsSupported(
            const GameEngine::Platform::TextRasterizationRequest&) const override
        {
            return true;
        }

        [[nodiscard]] bool LayoutText(
            const GameEngine::Platform::TextRasterizationRequest& request,
            GameEngine::Platform::TextGlyphLayout& layout) override
        {
            GameEngine::Platform::PositionedGlyphRun run;
            run.fontKey = 1;
            run.fontSize = request.fontSize;
            float pen = 0.0f;
            for (const char character : request.text)
            {
                GameEngine::Platform::PositionedGlyph glyph;
                glyph.glyphId = static_cast<std::uint16_t>(static_cast<unsigned char>(character));
                glyph.x = pen;
                glyph.y = 0.0f;
                run.glyphs.push_back(glyph);
                pen += 20.0f;
            }
            layout.runs.push_back(std::move(run));
            layout.width = static_cast<unsigned int>(pen) + 1;
            layout.height = 24;
            return true;
        }

        [[nodiscard]] bool RasterizeGlyph(
            const std::uint64_t,
            const float fontSize,
            const std::uint16_t glyphId,
            GameEngine::Platform::RasterizedGlyph& result) override
        {
            if (glyphId == static_cast<std::uint16_t>(' '))
            {
                return true;
            }
            const bool wholePage = fontSize >= 100.0f;
            result.width = wholePage ? 1000u : 6u + glyphId % 11u;
            result.height = wholePage ? 1000u : 5u + glyphId % 7u;
            result.alphaPixels.assign(
                static_cast<std::size_t>(result.width) * result.height, MarkerFor(glyphId));
            result.offsetX = 0.0f;
            result.offsetY = -static_cast<float>(result.height);
            return true;
        }
    };

    /// <summary>
    /// 배치가 실제로 가리키는 글리프들이다. quad의 UV를 페이지 픽셀로 되돌려 그 사각형을 읽고,
    /// 한 값으로 고르게 칠해져 있으면 그 값을 담는다. 사각형이 페이지 밖으로 나가거나, 비어
    /// 있거나, 두 값이 섞여 있으면 corrupt가 올라간다 — 그 셋이 각각 낡은 좌표, 지워진 슬롯,
    /// 겹쳐 놓인 슬롯의 모습이다.
    /// </summary>
    struct InkReading
    {
        std::multiset<std::byte> markers;
        int corrupt = 0;
    };

    [[nodiscard]] InkReading ReadInk(const GameEngine::Rendering::ShapedText& shaped)
    {
        constexpr float pageSize = static_cast<float>(GameEngine::Rendering::GlyphAtlas::PageSize);
        InkReading reading;
        for (const GameEngine::Rendering::ShapedTextRun& run : shaped.runs)
        {
            if (!run.page || !run.glyphs || !run.page->IsValid())
            {
                ++reading.corrupt;
                continue;
            }
            const GameEngine::Rendering::RasterizedTextImage& page = *run.page;
            for (const GameEngine::Rendering::TextGlyphQuad& quad : *run.glyphs)
            {
                const auto x = static_cast<unsigned int>(quad.u * pageSize + 0.5f);
                const auto y = static_cast<unsigned int>(quad.v * pageSize + 0.5f);
                const auto width = static_cast<unsigned int>(quad.uWidth * pageSize + 0.5f);
                const auto height = static_cast<unsigned int>(quad.vHeight * pageSize + 0.5f);
                if (width == 0 || height == 0 ||
                    x + width > page.width || y + height > page.height)
                {
                    ++reading.corrupt;
                    continue;
                }
                const std::byte first =
                    page.alphaPixels[static_cast<std::size_t>(y) * page.width + x];
                bool uniform = first != std::byte{ 0 };
                for (unsigned int row = 0; uniform && row < height; ++row)
                {
                    for (unsigned int column = 0; column < width; ++column)
                    {
                        const std::byte value = page.alphaPixels[
                            (static_cast<std::size_t>(y) + row) * page.width + x + column];
                        if (value != first)
                        {
                            uniform = false;
                            break;
                        }
                    }
                }
                if (!uniform)
                {
                    ++reading.corrupt;
                    continue;
                }
                reading.markers.insert(first);
            }
        }
        return reading;
    }

    /// <summary>그 문자열이 남겨야 할 잉크다. 공백은 잉크가 없으므로 세지 않는다.</summary>
    [[nodiscard]] std::multiset<std::byte> ExpectedInk(const std::string_view text)
    {
        std::multiset<std::byte> expected;
        for (const char character : text)
        {
            if (character != ' ')
            {
                expected.insert(MarkingRasterizer::MarkerFor(
                    static_cast<std::uint16_t>(static_cast<unsigned char>(character))));
            }
        }
        return expected;
    }

    [[nodiscard]] GameEngine::Platform::TextRasterizationRequest MakeRequest(
        const std::string_view text, const float fontSize)
    {
        GameEngine::Platform::TextRasterizationRequest request;
        request.text = std::string(text);
        request.fontSize = fontSize;
        return request;
    }
}

bool RunGlyphAtlasDropoutTests()
{
    namespace Rendering = GameEngine::Rendering;

    Rendering::TextRasterizationCache cache(std::make_unique<MarkingRasterizer>());

    // 같은 글자를 여러 번 포함하여 아틀라스 슬롯을 재사용하는 경로를 검사한다.
    constexpr std::string_view line = "Loaded an asset. path=Sprites/button 32.png";

    cache.BeginFrame();
    const std::shared_ptr<const Rendering::ShapedText> first =
        cache.Resolve(MakeRequest(line, 16.0f));
    const InkReading firstReading = first ? ReadInk(*first) : InkReading{};
    const bool everyGlyphDrawnOnce = first && firstReading.corrupt == 0 &&
        firstReading.markers == ExpectedInk(line);

    // 아틀라스를 페이지 상한 너머로 태운다. 한 페이지에 하나씩만 들어가는 글리프를 크기를
    // 바꿔 가며 요청하면, 페이지가 하나씩 열리다 상한에서 아틀라스가 처음부터 다시 시작한다.
    for (std::size_t index = 0; index <= Rendering::GlyphAtlas::MaximumPages; ++index)
    {
        cache.BeginFrame();
        const std::shared_ptr<const Rendering::ShapedText> filler =
            cache.Resolve(MakeRequest("W", 100.0f + static_cast<float>(index)));
        if (!Expect(filler != nullptr, "a page-filling glyph should still be laid out"))
        {
            return false;
        }
    }

    // 다시 시작한 뒤의 같은 줄이다. 낡은 좌표를 그대로 쓰면 여기서 값이 다르거나 0이고, 그것이
    // 화면에서 특정 글자만 빠진 줄로 보인다.
    cache.BeginFrame();
    const std::shared_ptr<const Rendering::ShapedText> afterRestart =
        cache.Resolve(MakeRequest(line, 16.0f));
    const InkReading restartReading = afterRestart ? ReadInk(*afterRestart) : InkReading{};
    const bool everyGlyphSurvivedRestart = afterRestart && restartReading.corrupt == 0 &&
        restartReading.markers == ExpectedInk(line);

    // 다시 시작하기 전에 받아 간 배치도 그대로여야 한다. 이미 프레임에 실려 GPU로 가는 중인
    // 배치가 그것이고, 아틀라스가 옛 페이지를 shared_ptr로 살려 두는 이유다.
    const InkReading heldReading = first ? ReadInk(*first) : InkReading{};
    const bool heldLayoutStillValid = first && heldReading.corrupt == 0 &&
        heldReading.markers == ExpectedInk(line);

    // 크기가 다른 같은 글자는 서로 다른 슬롯이다. 한 크기의 배치가 다른 크기의 픽셀을 가리키면
    // 글자가 뭉개져 보이고, 그것도 좌표가 낡았을 때의 모습이다.
    cache.BeginFrame();
    const std::shared_ptr<const Rendering::ShapedText> small =
        cache.Resolve(MakeRequest("pull", 12.0f));
    const std::shared_ptr<const Rendering::ShapedText> large =
        cache.Resolve(MakeRequest("pull", 24.0f));
    const InkReading smallReading = small ? ReadInk(*small) : InkReading{};
    const InkReading largeReading = large ? ReadInk(*large) : InkReading{};
    const bool sizesKeepTheirOwnSlots = small && large &&
        smallReading.corrupt == 0 && largeReading.corrupt == 0 &&
        smallReading.markers == ExpectedInk("pull") &&
        largeReading.markers == ExpectedInk("pull");

    // 한 문자열이 여러 아틀라스 페이지를 쓰면 페이지마다 run과 draw가 필요하다.
    // 한 페이지가 빠지면 그 페이지의 글자가 사라진다. 글리프마다 페이지를 하나씩 쓰도록
    // 크기를 정하여 모든 페이지가 그려지는지 검사한다.
    cache.BeginFrame();
    const std::shared_ptr<const Rendering::ShapedText> acrossPages =
        cache.Resolve(MakeRequest("xyz", 140.0f));
    const InkReading acrossReading = acrossPages ? ReadInk(*acrossPages) : InkReading{};
    const bool everyPageOfALineIsDrawn = acrossPages && acrossPages->runs.size() == 3 &&
        acrossReading.corrupt == 0 && acrossReading.markers == ExpectedInk("xyz");

    // 실제 엔진 래스터라이저와 번들 폰트에서도 잉크가 있어야 할 글자가 빈 그림으로 돌아오지 않는지 확인한다.
    // 아틀라스의 래스터화 실패는 문자열 전체를, 백엔드 업로드 실패는 페이지 전체를 잃게 한다.
    // 글리프 하나가 잉크 없음으로 처리되면 그 결과도 캐시에 남으므로 글리프별 잉크를 검사한다.
    const std::unique_ptr<GameEngine::Platform::ITextRasterizer> platformRasterizer =
        TestSupport::CreateTestTextRasterizer();
    bool platformReady = platformRasterizer != nullptr;
    int inklessGlyphs = 0;
    int measuredGlyphs = 0;
    if (platformReady)
    {
        // 공백은 잉크가 없는 것이 정답이므로 뺀다. 남은 글자는 전부 잉크가 있어야 한다.
        GameEngine::Platform::TextRasterizationRequest request =
            MakeRequest("Loadedanasset.path=Sprites/button32.png", 13.0f);
        for (float fontSize = 11.0f; fontSize <= 24.0f; fontSize += 1.0f)
        {
            request.fontSize = fontSize;
            GameEngine::Platform::TextGlyphLayout layout;
            if (!platformRasterizer->LayoutText(request, layout))
            {
                platformReady = false;
                break;
            }
            for (const GameEngine::Platform::PositionedGlyphRun& run : layout.runs)
            {
                for (const GameEngine::Platform::PositionedGlyph& glyph : run.glyphs)
                {
                    GameEngine::Platform::RasterizedGlyph rasterized;
                    if (!platformRasterizer->RasterizeGlyph(
                            run.fontKey, run.fontSize, glyph.glyphId, rasterized))
                    {
                        ++inklessGlyphs;
                        continue;
                    }
                    ++measuredGlyphs;
                    if (rasterized.width == 0 || rasterized.height == 0)
                    {
                        ++inklessGlyphs;
                    }
                }
            }
        }
    }
    const bool everyInkedCharacterHasInk =
        platformReady && measuredGlyphs > 0 && inklessGlyphs == 0;

    return Expect(
            everyGlyphDrawnOnce,
            "every inked glyph of a line should point at its own pixels") &&
        Expect(
            everyGlyphSurvivedRestart,
            "a line reshaped after an atlas restart should point at its new pixels") &&
        Expect(
            heldLayoutStillValid,
            "a layout taken before the restart should keep pointing at valid pixels") &&
        Expect(
            sizesKeepTheirOwnSlots,
            "the same glyph at two sizes should point at two slots, each with its own ink") &&
        Expect(
            everyPageOfALineIsDrawn,
            "a line whose glyphs span several pages should keep a run for every page") &&
        Expect(
            everyInkedCharacterHasInk,
            "the platform rasterizer should give ink to every character that has ink");
}

static const TestSupport::Registration gGlyphAtlasDropoutTests{
    "RenderCache", "glyph atlas dropout tests should pass", RunGlyphAtlasDropoutTests };
