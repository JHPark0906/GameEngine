#include "pch.h"
#include "GlyphAtlas.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../Assets/ResourceId.h"
#include "../Diagnostics/Debug.h"

namespace GameEngine::Rendering
{

namespace
{
    /// <summary>글리프 사이의 여백 픽셀이다. 이웃 슬롯의 픽셀이 필터링에 새어들지 않게 한다.</summary>
    constexpr unsigned int GlyphPadding = 1;

    /// <summary>실패 기록의 상한이다. 넘으면 비우고, 같은 실패가 다시 오면 다시 기록된다.</summary>
    constexpr std::size_t MaximumFailedKeys = 4096;

    /// <summary>선반의 높이 양자화다. 비슷한 높이의 글리프가 같은 선반을 쓰게 한다.</summary>
    constexpr unsigned int ShelfHeightStep = 8;

    /// <summary>
    /// 페이지 하나와 그 선반들이다. 선반 패킹: 페이지는 가로 선반들로 잘리고, 글리프는 자기
    /// 높이가 들어가는 선반의 커서 위치에 놓인다. 완전한 최적 패킹은 아니지만 글리프처럼 높이가
    /// 몇 단계로 몰리는 입력에는 충분히 조밀하고, 무엇보다 이미 놓인 슬롯을 결코 움직이지
    /// 않는다 — 움직이면 캐시된 UV가 전부 거짓이 된다.
    /// </summary>
    struct AtlasPage
    {
        /// <summary>
        /// 발행된 페이지 그림이다. <b>const인 것이 계약이다</b>: 프레임과 캐시가 이 포인터를
        /// 복사해 가고, 렌더 스레드가 그것을 읽는 동안 게임 스레드는 다음 프레임을 만든다.
        /// 여기에 글리프를 더하려면 새 그림을 만들어 이 자리를 바꿔 끼운다.
        /// </summary>
        std::shared_ptr<const RasterizedTextImage> image;

        struct Shelf
        {
            unsigned int y = 0;
            unsigned int height = 0;
            unsigned int cursorX = 0;
        };
        std::vector<Shelf> shelves;
        unsigned int nextShelfY = 0;

        /// <summary>slot 크기가 들어갈 자리를 찾아 좌표를 답한다. 없으면 false다.</summary>
        [[nodiscard]] bool TryAllocate(
            const unsigned int width, const unsigned int height,
            unsigned int& x, unsigned int& y)
        {
            const unsigned int paddedWidth = width + GlyphPadding;
            const unsigned int paddedHeight = height + GlyphPadding;
            if (paddedWidth > GlyphAtlas::PageSize)
            {
                return false;
            }
            const unsigned int shelfHeight =
                ((paddedHeight + ShelfHeightStep - 1) / ShelfHeightStep) * ShelfHeightStep;
            for (Shelf& shelf : shelves)
            {
                if (shelf.height >= paddedHeight && shelf.height <= shelfHeight * 2 &&
                    shelf.cursorX + paddedWidth <= GlyphAtlas::PageSize)
                {
                    x = shelf.cursorX;
                    y = shelf.y;
                    shelf.cursorX += paddedWidth;
                    return true;
                }
            }
            if (nextShelfY + shelfHeight > GlyphAtlas::PageSize)
            {
                return false;
            }
            Shelf shelf;
            shelf.y = nextShelfY;
            shelf.height = shelfHeight;
            shelf.cursorX = paddedWidth;
            nextShelfY += shelfHeight;
            x = 0;
            y = shelf.y;
            shelves.push_back(shelf);
            return true;
        }
    };

    /// <summary>
    /// 페이지 id는 프로세스 전역으로 발급된다. 아틀라스 인스턴스는 여럿이고 — 장면 프론트엔드와
    /// 에디터 UI가 각자 캐시를 갖는다 — 백엔드의 id 기반 텍스처 캐시는 그 전부를 한 이름
    /// 공간에서 본다.
    /// </summary>
    std::atomic<std::uint64_t> gNextPageId{ 0 };

    [[nodiscard]] std::shared_ptr<RasterizedTextImage> MakePage()
    {
        auto image = std::make_shared<RasterizedTextImage>();
        image->id = Assets::MakeResourceId(
            Assets::ResourceIdDomain::Text, gNextPageId.fetch_add(1) + 1);
        image->revision = 1;
        image->width = GlyphAtlas::PageSize;
        image->height = GlyphAtlas::PageSize;
        image->alphaPixels.assign(
            static_cast<std::size_t>(GlyphAtlas::PageSize) * GlyphAtlas::PageSize,
            std::byte{ 0 });
        return image;
    }
}

struct GlyphAtlas::Implementation
{
    std::vector<AtlasPage> pages;
    std::unordered_map<GlyphKey, GlyphAtlasSlot, GlyphKeyHash> slots;
    std::unordered_set<GlyphKey, GlyphKeyHash> failed;
    std::uint64_t epoch = 0;
    /// <summary>발행된 그림이 바뀔 때마다 오른다. 배치를 캐시해 둔 쪽이 이 값으로 낡음을 안다.</summary>
    std::uint64_t generation = 0;
    /// <summary>지금까지 만든 페이지 그림의 수다. 예열 뒤 이 값이 멈추는지를 시험이 센다.</summary>
    std::uint64_t snapshots = 0;

    /// <summary>글리프를 어느 페이지든 들어가는 곳에 넣는다. 상한에 닿으면 에포크를 올린다.</summary>
    [[nodiscard]] bool Place(
        const Platform::RasterizedGlyph& glyph,
        AtlasPage*& page, unsigned int& x, unsigned int& y)
    {
        for (AtlasPage& candidate : pages)
        {
            if (candidate.TryAllocate(glyph.width, glyph.height, x, y))
            {
                page = &candidate;
                return true;
            }
        }
        if (pages.size() >= MaximumPages)
        {
            // 처음부터 다시: 슬롯과 페이지 목록을 버리지만 페이지 객체는 shared_ptr로 살아
            // 있어, 옛 배치를 쥔 캐시와 in-flight 프레임은 계속 유효한 픽셀을 본다. 이 뒤의
            // resolve들이 쓰이는 글리프들을 새 페이지에 다시 모은다.
            ++epoch;
            ++generation;
            slots.clear();
            pages.clear();
            Diagnostics::Debug::LogWarning(
                "The glyph atlas reached its page limit and restarted. epoch=", epoch);
        }
        AtlasPage newPage;
        newPage.image = MakePage();
        ++snapshots;
        ++generation;
        pages.push_back(std::move(newPage));
        page = &pages.back();
        return page->TryAllocate(glyph.width, glyph.height, x, y);
    }
};

GlyphAtlas::GlyphAtlas()
    : mImplementation(std::make_unique<Implementation>())
{
}

GlyphAtlas::~GlyphAtlas() = default;

const GlyphAtlasSlot* GlyphAtlas::Resolve(
    const GlyphKey& key,
    const std::function<bool(Platform::RasterizedGlyph&)>& rasterize)
{
    Implementation& data = *mImplementation;
    if (const auto existing = data.slots.find(key); existing != data.slots.end())
    {
        return &existing->second;
    }
    if (data.failed.contains(key))
    {
        return nullptr;
    }

    Platform::RasterizedGlyph glyph;
    if (!rasterize(glyph))
    {
        if (data.failed.size() >= MaximumFailedKeys)
        {
            data.failed.clear();
        }
        data.failed.insert(key);
        return nullptr;
    }

    GlyphAtlasSlot slot;
    slot.width = glyph.width;
    slot.height = glyph.height;
    slot.offsetX = glyph.offsetX;
    slot.offsetY = glyph.offsetY;
    slot.hasInk = glyph.width > 0 && glyph.height > 0;
    if (slot.hasInk)
    {
        AtlasPage* page = nullptr;
        unsigned int x = 0;
        unsigned int y = 0;
        if (!data.Place(glyph, page, x, y))
        {
            // 페이지 한 장보다 큰 글리프다. 실패로 기록해 매 프레임 다시 재지 않는다.
            Diagnostics::Debug::LogError(
                "A glyph does not fit an atlas page. width=", glyph.width,
                ", height=", glyph.height);
            if (data.failed.size() >= MaximumFailedKeys)
            {
                data.failed.clear();
            }
            data.failed.insert(key);
            return nullptr;
        }

        // 자란 페이지는 새 그림이다. 이미 발행된 픽셀은 바꾸지 않아야 렌더 스레드가 읽는 동안
        // 게임 스레드가 다음 프레임을 만들어도 RenderFrame의 불변 스냅샷 계약을 지킬 수 있다.
        //
        // id는 같은 논리적 페이지를 가리킨다. 백엔드는 revision이 오르면 새 텍스처를 만드는 대신
        // 해당 페이지의 픽셀을 제자리에 다시 올린다.
        const std::shared_ptr<const RasterizedTextImage> published = page->image;
        auto grown = std::make_shared<RasterizedTextImage>(*published);
        for (unsigned int row = 0; row < glyph.height; ++row)
        {
            std::copy_n(
                glyph.alphaPixels.data() + static_cast<std::size_t>(row) * glyph.width,
                glyph.width,
                grown->alphaPixels.data() +
                    (static_cast<std::size_t>(y) + row) * PageSize + x);
        }
        ++grown->revision;
        page->image = grown;
        ++data.snapshots;
        ++data.generation;

        // 이 페이지를 가리키던 슬롯들을 새 판으로 옮긴다. 그러지 않으면 한 id 아래 두 그림이
        // 같은 프레임에 실려, 백엔드가 그 둘을 번갈아 올리게 된다.
        for (auto& [existingKey, existingSlot] : data.slots)
        {
            if (existingSlot.page == published)
            {
                existingSlot.page = grown;
            }
        }

        constexpr float pageSize = static_cast<float>(PageSize);
        slot.page = grown;
        slot.u = static_cast<float>(x) / pageSize;
        slot.v = static_cast<float>(y) / pageSize;
        slot.uWidth = static_cast<float>(glyph.width) / pageSize;
        slot.vHeight = static_cast<float>(glyph.height) / pageSize;
    }
    return &data.slots.emplace(key, std::move(slot)).first->second;
}

std::uint64_t GlyphAtlas::GetGeneration() const
{
    return mImplementation->generation;
}

std::uint64_t GlyphAtlas::GetSnapshotCount() const
{
    return mImplementation->snapshots;
}

std::uint64_t GlyphAtlas::GetEpoch() const
{
    return mImplementation->epoch;
}

std::size_t GlyphAtlas::GetPageCount() const
{
    return mImplementation->pages.size();
}

}
