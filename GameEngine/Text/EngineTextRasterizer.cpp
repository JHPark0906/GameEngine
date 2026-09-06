#include "pch.h"
#include "EngineTextRasterizer.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "CffFont.h"
#include "GlyphRasterizer.h"
#include "LineBreaker.h"
#include "TrueTypeOutlines.h"

namespace GameEngine::Text
{

namespace
{
    /// <summary>
    /// 한 face에서 글리프 하나의 윤곽선을 꺼낸다. 형식을 아는 자리는 이 함수 하나다.
    ///
    /// 글리프 0은 <b>두부</b>다. 폰트가 그 자리에 들고 있는 것을 쓰지 않고 우리가 만든 네모를
    /// 그린다 — 동봉 폰트만 봐도 한쪽은 비어 있고 한쪽은 차 있어서, 폰트에 맡기면 없는 글자가
    /// 어떤 폰트에서는 보이고 어떤 폰트에서는 안 보인다.
    /// </summary>
    [[nodiscard]] bool ReadOutline(
        const FontFace& face, const std::uint16_t glyphId, GlyphOutline& outline)
    {
        if (glyphId == 0)
        {
            outline = MakeTofuOutline(face.GetUnitsPerEm());
            return true;
        }
        if (face.GetOutlineFormat() == FontFace::OutlineFormat::TrueType)
        {
            return GetTrueTypeGlyphOutline(face, glyphId, outline);
        }
        CffFont cff;
        CharstringResult charstring;
        if (!cff.Parse(face.GetTable("CFF ")) || !cff.GetGlyphOutline(glyphId, charstring))
        {
            return false;
        }
        outline = std::move(charstring.outline);
        return true;
    }

    /// <summary>
    /// 한 줄의 높이와 그 안에서 베이스라인이 앉는 자리다. 폰트 단위가 아니라 픽셀이다.
    /// hhea의 어센더, 디센더, 줄 간격으로 줄 높이와 베이스라인을 계산한다.
    /// </summary>
    struct LineMetrics
    {
        float height = 0.0f;
        float baseline = 0.0f;
    };

    [[nodiscard]] LineMetrics MeasureLine(
        const FontFace& face, const float fontSize, const float lineSpacing)
    {
        const auto unitsPerEm = static_cast<float>(face.GetUnitsPerEm());
        if (unitsPerEm <= 0.0f)
        {
            return LineMetrics{ fontSize, fontSize };
        }
        const FontFace::VerticalMetrics& metrics = face.GetVerticalMetrics();
        const float scale = fontSize / unitsPerEm;
        const float ascender = static_cast<float>(metrics.hheaAscender) * scale;
        const float descender = static_cast<float>(-metrics.hheaDescender) * scale;
        const float gap = static_cast<float>(metrics.hheaLineGap) * scale;
        LineMetrics line;
        line.height = (ascender + descender + gap) * (lineSpacing > 0.0f ? lineSpacing : 1.0f);
        line.baseline = ascender;
        return line;
    }
}

EngineTextRasterizer::EngineTextRasterizer() = default;
EngineTextRasterizer::~EngineTextRasterizer() = default;

bool EngineTextRasterizer::Initialize()
{
    // 준비할 것이 없다. 폰트는 등록될 때 읽히고, 그 밖에 붙잡을 플랫폼 자원이 없는 것이 이
    // 구현의 요점이다.
    return true;
}

bool EngineTextRasterizer::RegisterFont(
    const std::string_view familyAlias, const std::span<const std::byte> fontBytes)
{
    return mFonts.Register(familyAlias, fontBytes);
}

bool EngineTextRasterizer::IsSupported(const Platform::TextRasterizationRequest& request) const
{
    // 이 구현이 거절하는 것은 배치가 뜻을 잃는 입력뿐이다. 글자가 없는 것은 거절이 아니라 빈
    // 결과이므로 여기서 걸러 내지 않는다.
    return request.fontSize > 0.0f && mFonts.GetRegisteredCount() > 0;
}

bool EngineTextRasterizer::LayoutText(
    const Platform::TextRasterizationRequest& request, Platform::TextGlyphLayout& layout)
{
    layout = Platform::TextGlyphLayout{};
    if (request.fontSize <= 0.0f || mFonts.GetRegisteredCount() == 0)
    {
        return false;
    }
    if (request.text.empty())
    {
        return true;
    }

    std::vector<BreakItem> items = FindBreakOpportunities(request.text);
    if (items.empty())
    {
        // 빈 글자가 아닌데 덩어리가 없다면 UTF-8이 아니다. 지어내는 대신 실패한다.
        return false;
    }

    // 글자마다 어느 face로 그릴지 먼저 정하고, 그 face의 진행폭으로 폭을 채운다. 대체 폰트가
    // 끼어들 수 있으므로 폭은 face마다 달라진다 — 하나의 배율로 전부 재면 대체된 글자의 자리가
    // 어긋난다.
    std::vector<FontLibrary::GlyphSelection> selections(items.size());
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        if (items[index].isMandatoryBreak)
        {
            continue;
        }
        if (!mFonts.SelectGlyph(request.fontFamily, items[index].codePoint, selections[index]))
        {
            return false;
        }
        const FontFace* const face = selections[index].face;
        const auto unitsPerEm = static_cast<float>(face->GetUnitsPerEm());
        const float scale = unitsPerEm > 0.0f ? request.fontSize / unitsPerEm : 0.0f;
        items[index].advance =
            static_cast<float>(face->GetAdvanceWidth(selections[index].glyphId)) * scale;
    }

    const std::vector<LineRange> lines = BreakIntoLines(items, request.maxWidth);
    if (lines.empty())
    {
        return true;
    }

    // 줄 높이는 요청한 패밀리의 것으로 잰다. 대체 폰트가 섞여도 줄 간격이 글자마다 달라지지
    // 않게 하려는 것이다 — 그러지 않으면 한 문단 안에서 줄 간격이 들쭉날쭉해진다.
    const FontFace* const primary = mFonts.Find(request.fontFamily) != nullptr
        ? mFonts.Find(request.fontFamily)
        : mFonts.GetFace(0);
    const LineMetrics lineMetrics =
        MeasureLine(*primary, request.fontSize, request.lineSpacing);

    float widest = 0.0f;
    for (const LineRange& line : lines)
    {
        widest = (std::max)(widest, line.width);
    }
    // 줄바꿈 폭이 주어졌으면 그것이 블록의 폭이다. 정렬은 그 폭 안에서 일어나므로, 가장 긴 줄을
    // 폭으로 삼으면 오른쪽 정렬이 아무것도 하지 않게 된다.
    const float blockWidth = request.maxWidth > 0.0f ? request.maxWidth : widest;

    for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
    {
        const LineRange& line = lines[lineIndex];
        float penX = 0.0f;
        switch (request.alignment)
        {
        case Platform::TextAlignment::Center:
            penX = (blockWidth - line.width) * 0.5f;
            break;
        case Platform::TextAlignment::Right:
            penX = blockWidth - line.width;
            break;
        case Platform::TextAlignment::Left:
        default:
            break;
        }
        const float penY =
            static_cast<float>(lineIndex) * lineMetrics.height + lineMetrics.baseline;

        const float lineTop = static_cast<float>(lineIndex) * lineMetrics.height;

        for (std::size_t item = line.firstItem; item < line.firstItem + line.itemCount; ++item)
        {
            // 편집 모델의 인덱스는 UTF-8 바이트 위치다. 글리프 번호나 글자 수로 바꾸면
            // 한글처럼 여러 바이트인 문자의 캐럿·조합 밑줄이 다른 위치를 가리킨다.
            layout.caretStops.push_back({ items[item].byteOffset, penX, lineTop, lineMetrics.height });
            if (items[item].isMandatoryBreak)
            {
                continue;
            }
            const FontLibrary::GlyphSelection& selection = selections[item];

            // 같은 face가 이어지는 동안은 한 run이다. face가 바뀌면 새 run을 연다 — 래스터화가
            // 그 face를 다시 찾아야 하고, run의 fontKey가 그 정체를 나른다.
            const auto key = static_cast<std::uint64_t>(selection.faceIndex);
            if (layout.runs.empty() || layout.runs.back().fontKey != key)
            {
                Platform::PositionedGlyphRun run;
                run.fontKey = key;
                run.fontSize = request.fontSize;
                layout.runs.push_back(std::move(run));
            }

            Platform::PositionedGlyph glyph;
            glyph.glyphId = selection.glyphId;
            glyph.x = penX;
            glyph.y = penY;
            layout.runs.back().glyphs.push_back(glyph);
            penX += items[item].advance;
        }
        const std::size_t end = line.firstItem + line.itemCount;
        const std::size_t byteEnd = end > line.firstItem
            ? items[end - 1].byteOffset + items[end - 1].byteLength : request.text.size();
        layout.caretStops.push_back({ byteEnd, penX, lineTop, lineMetrics.height });
    }

    // 공백만 있는 필드도 실제 줄 높이와 캐럿 좌표를 가진 유효한 배치다.
    layout.width = (std::max)(1u, static_cast<unsigned int>(std::ceil(blockWidth)));
    layout.height = static_cast<unsigned int>(
        std::ceil(static_cast<float>(lines.size()) * lineMetrics.height));
    return true;
}

bool EngineTextRasterizer::RasterizeGlyph(
    const std::uint64_t fontKey,
    const float fontSize,
    const std::uint16_t glyphId,
    Platform::RasterizedGlyph& result)
{
    result = Platform::RasterizedGlyph{};
    const FontFace* const face = mFonts.GetFace(static_cast<std::size_t>(fontKey));
    if (!face || fontSize <= 0.0f)
    {
        return false;
    }

    GlyphOutline outline;
    if (!ReadOutline(*face, glyphId, outline))
    {
        return false;
    }

    const auto unitsPerEm = static_cast<float>(face->GetUnitsPerEm());
    CoverageBitmap bitmap;
    const RasterizerSettings settings;
    if (!RasterizeGlyphOutline(outline, fontSize / unitsPerEm, settings, bitmap))
    {
        return false;
    }

    result.alphaPixels = std::move(bitmap.alpha);
    result.width = bitmap.width;
    result.height = bitmap.height;
    result.offsetX = bitmap.originX;
    result.offsetY = bitmap.originY;
    return true;
}

}
