#include "pch.h"
#include "TextRasterizationCache.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string_view>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FrameBoundCache.h"
#include "GlyphAtlas.h"
#include "RenderResourceCachePolicy.h"
#include "TextRasterizationKey.h"
#include "../Platform/ITextRasterizer.h"
#include "../Diagnostics/Debug.h"

namespace GameEngine::Rendering
{

namespace
{
    /// <summary>캐시된 배치 하나의 예산 비용이다: quad 벡터와 고정 몫의 근사다.</summary>
    [[nodiscard]] std::size_t ShapedTextByteSize(const ShapedText& shaped)
    {
        std::size_t bytes = sizeof(ShapedText) + shaped.caretStops.size() * sizeof(Platform::TextCaretStop);
        for (const ShapedTextRun& run : shaped.runs)
        {
            bytes += sizeof(ShapedTextRun) +
                (run.glyphs ? run.glyphs->size() * sizeof(TextGlyphQuad) : 0);
        }
        return bytes;
    }

    /// <summary>캐시 항목이다: 배치와, 그것이 어느 아틀라스 에포크의 페이지를 가리키는지.</summary>
    struct CachedShapedText
    {
        std::shared_ptr<const ShapedText> shaped;
        /// <summary>
        /// 이 배치를 만들 때의 아틀라스 세대다. 세대가 바뀌었다는 것은 발행된 페이지 그림이
        /// 바뀌었다는 뜻이고, 그러면 이 배치는 옛 그림을 가리킨다 — 그리기에는 유효하지만
        /// 계속 쥐고 있으면 한 id 아래 두 그림이 같은 프레임에 실린다.
        /// </summary>
        std::uint64_t atlasGeneration = 0;
    };
}

struct TextRasterizationCache::Implementation
{
    std::unique_ptr<Platform::ITextRasterizer> rasterizer;
    /// <summary>첫 요청이 래스터라이저 초기화를 시도한 뒤 설정된다.</summary>
    bool initializationAttempted = false;

    GlyphAtlas atlas;

    FrameBoundCache<TextRasterizationKey, CachedShapedText, TextRasterizationKeyHash> shaped{
        TextCacheBudget.maximumEntries, TextCacheBudget.maximumBytes };

    BoundedKeySet<TextRasterizationKey, TextRasterizationKeyHash> failed{ TextCacheBudget };
    BoundedKeySet<TextRasterizationKey, TextRasterizationKeyHash> capacityRejected{ TextCacheBudget };
    CapacityWarningThrottle capacityWarning;

    /// <summary>
    /// 주입받은 래스터라이저를 처음 쓸 때 초기화한다. 텍스트가 없는 프로젝트는 플랫폼 폰트
    /// 스택의 비용을 치르면 안 되고, 그것을 시작할 수 없는 머신에서는 렌더링 전체가 실패하는
    /// 대신 텍스트만 잃어야 한다.
    /// </summary>
    [[nodiscard]] Platform::ITextRasterizer* GetRasterizer()
    {
        if (!initializationAttempted)
        {
            initializationAttempted = true;
            if (rasterizer && !rasterizer->Initialize())
            {
                Diagnostics::Debug::LogError(
                    "Failed to initialize the platform text rasterizer. Text will not be rendered.");
                rasterizer.reset();
            }
        }
        return rasterizer.get();
    }

    /// <summary>
    /// 레이아웃의 글리프들을 아틀라스 슬롯으로 바꿔 페이지별 quad 목록으로 모은다.
    /// 글리프 사각형을 정수 픽셀로 스냅해 스크린 텍스트의 픽셀 정렬을 유지한다.
    /// </summary>
    [[nodiscard]] std::shared_ptr<const ShapedText> Shape(
        Platform::ITextRasterizer& shaper, const Platform::TextGlyphLayout& layout)
    {
        auto result = std::make_shared<ShapedText>();
        result->width = layout.width;
        result->height = layout.height;
        result->caretStops = layout.caretStops;
        const float halfWidth = static_cast<float>(layout.width) * 0.5f;
        const float halfHeight = static_cast<float>(layout.height) * 0.5f;

        // 페이지 포인터 순서가 아니라 페이지 id 순서로 run을 내면, 같은 문자열은 언제나 같은
        // 순서의 run이 된다 — 저장 결정성과 같은 이유의 결정성이다.
        std::map<std::uint64_t,
            std::pair<std::shared_ptr<const RasterizedTextImage>,
                std::shared_ptr<std::vector<TextGlyphQuad>>>> pages;

        for (const Platform::PositionedGlyphRun& run : layout.runs)
        {
            for (const Platform::PositionedGlyph& glyph : run.glyphs)
            {
                GlyphKey key;
                key.fontKey = run.fontKey;
                key.fontSizeBits = std::bit_cast<std::uint32_t>(run.fontSize);
                key.glyphId = glyph.glyphId;
                const GlyphAtlasSlot* const slot = atlas.Resolve(
                    key,
                    [&shaper, &run, &glyph](Platform::RasterizedGlyph& rasterized)
                    {
                        return shaper.RasterizeGlyph(
                            run.fontKey, run.fontSize, glyph.glyphId, rasterized);
                    });
                if (!slot)
                {
                    return nullptr;
                }
                if (!slot->hasInk)
                {
                    continue;
                }

                const float left = std::round(glyph.x + slot->offsetX);
                const float top = std::round(glyph.y + slot->offsetY);
                TextGlyphQuad quad;
                quad.width = static_cast<float>(slot->width);
                quad.height = static_cast<float>(slot->height);
                quad.centerX = left + quad.width * 0.5f - halfWidth;
                quad.centerY = halfHeight - (top + quad.height * 0.5f);
                quad.u = slot->u;
                quad.v = slot->v;
                quad.uWidth = slot->uWidth;
                quad.vHeight = slot->vHeight;

                auto& page = pages[slot->page->id];
                if (!page.second)
                {
                    page.second = std::make_shared<std::vector<TextGlyphQuad>>();
                }
                // 이 문자열을 배치하는 동안에도 페이지가 자랄 수 있고, 자란다는 것은 새 그림이
                // 생긴다는 뜻이다. 그때마다 최신 판을 쥔다 — 먼저 본 판에는 나중 글리프의
                // 픽셀이 없으므로, 처음 본 것을 붙들면 그 글자들이 빈자리를 가리킨다.
                page.first = slot->page;
                page.second->push_back(quad);
            }
        }

        result->runs.reserve(pages.size());
        for (auto& [pageId, page] : pages)
        {
            result->runs.push_back({ std::move(page.first), std::move(page.second) });
        }
        return result;
    }
};

TextRasterizationCache::TextRasterizationCache(
    std::unique_ptr<Platform::ITextRasterizer> rasterizer)
    : mImplementation(std::make_unique<Implementation>())
{
    mImplementation->rasterizer = std::move(rasterizer);
}

TextRasterizationCache::~TextRasterizationCache() = default;

bool TextRasterizationCache::RegisterFont(
    const std::string_view familyAlias, const std::span<const std::byte> fontBytes)
{
    Platform::ITextRasterizer* const rasterizer = mImplementation->GetRasterizer();
    return rasterizer && rasterizer->RegisterFont(familyAlias, fontBytes);
}

std::size_t TextRasterizationCache::GetShapedEntryCount() const
{
    return mImplementation->shaped.GetSize();
}

void TextRasterizationCache::BeginFrame()
{
    mImplementation->shaped.BeginFrame();

    // A capacity rejection describes one frame's contention and must be retried next frame. An
    // unsupported or failed request stays recorded so it is not attempted again every frame.
    mImplementation->capacityRejected.Clear();
    mImplementation->capacityWarning.BeginFrame();
}

std::shared_ptr<const ShapedText> TextRasterizationCache::Resolve(
    const Platform::TextRasterizationRequest& request)
{
    Implementation& data = *mImplementation;
    const TextRasterizationKey key = MakeTextRasterizationKey(request);
    CachedShapedText* const cached = data.shaped.Find(key);
    // 발행된 페이지 그림이 바뀌었으면 — 글리프가 더해졌거나 아틀라스가 다시 시작했으면 —
    // 캐시된 배치는 옛 그림을 가리킨다. 그리기는 여전히 유효하지만, 다시 배치해야 이 프레임의
    // 글리프들이 한 그림에 모인다.
    if (cached && cached->atlasGeneration == data.atlas.GetGeneration())
    {
        return cached->shaped;
    }
    if (data.failed.Contains(key) || data.capacityRejected.Contains(key))
    {
        return nullptr;
    }

    Platform::ITextRasterizer* const rasterizer = data.GetRasterizer();
    if (!rasterizer || !rasterizer->IsSupported(request))
    {
        data.failed.Record(key);
        return nullptr;
    }

    Platform::TextGlyphLayout layout;
    if (!rasterizer->LayoutText(request, layout))
    {
        data.failed.Record(key);
        return nullptr;
    }
    if (layout.width == 0 || layout.height == 0)
    {
        Diagnostics::Debug::LogError(
            "The text layout produced no dimensions. width=", layout.width,
            ", height=", layout.height);
        data.failed.Record(key);
        return nullptr;
    }

    const std::shared_ptr<const ShapedText> shaped = data.Shape(*rasterizer, layout);
    if (!shaped)
    {
        data.failed.Record(key);
        return nullptr;
    }

    if (cached)
    {
        // 낡은 항목의 자리 자체는 유효하므로 제자리에서 바꾼다. 예산 회계는 첫 삽입의 크기로
        // 남는데, 같은 문자열의 재배치는 quad 수가 같아 그 근사가 실제와 다르지 않다.
        cached->shaped = shaped;
        cached->atlasGeneration = data.atlas.GetGeneration();
        return shaped;
    }

    CachedShapedText entry{ shaped, data.atlas.GetGeneration() };
    const std::size_t byteSize = ShapedTextByteSize(*shaped);
    if (!data.shaped.MakeRoom(byteSize))
    {
        data.capacityRejected.Record(key);
        data.capacityWarning.Warn("The text shaping cache reached its entry or memory limit.");
        // The shaped text is still returned: this frame already paid to lay it out, and dropping
        // the draw would make text flicker whenever the cache is briefly full.
        return shaped;
    }
    // 배치마다 로그하지 않는다: 내용이 바뀌는 텍스트는 프레임마다 여기 오고, 그 로그가
    // 에디터 콘솔에 보이면 콘솔을 그리는 일이 새 로그를 낳는 되먹임이 된다.
    CachedShapedText* const inserted = data.shaped.Insert(key, std::move(entry), byteSize);
    return inserted ? inserted->shaped : shaped;
}

}
