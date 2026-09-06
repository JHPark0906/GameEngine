#include "RenderCacheTests.h"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

#include "Platform/ITextRasterizer.h"
#include "Platform/PlatformServices.h"
#include "Rendering/D3D12/D3D12DescriptorHeapAllocator.h"
#include "Rendering/D3D12/D3D12FrameResources.h"
#include "Rendering/FrameBoundCache.h"
#include "Rendering/GlyphAtlas.h"
#include "Rendering/RenderResourceCachePolicy.h"
#include "Rendering/ResolvedResourceCache.h"
#include "Rendering/TextRasterizationCache.h"
#include "Rendering/TextRasterizationKey.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
}

bool RunRenderResourceCacheTests()
{
    GameEngine::Rendering::FrameBoundCache<std::string, int> cache(2, 8);
    cache.BeginFrame();
    if (!cache.MakeRoom(4) || !cache.Insert("first", 10, 4))
    {
        return Expect(false, "frame-bound cache should admit its first entry");
    }
    int* const firstPointer = cache.Find("first");

    cache.BeginFrame();
    if (!cache.MakeRoom(4) || !cache.Insert("second", 20, 4))
    {
        return Expect(false, "frame-bound cache should admit an older-frame entry");
    }
    int* const firstPointerThisFrame = cache.Find("first");
    const bool rejectsEvictingCurrentFrameEntries = !cache.MakeRoom(4) &&
        firstPointer == firstPointerThisFrame && firstPointer && *firstPointer == 10;

    cache.BeginFrame();
    int* const retainedFirstPointer = cache.Find("first");
    const bool evictsOnlyOlderFrameEntries = cache.MakeRoom(4) &&
        cache.Find("second") == nullptr && retainedFirstPointer == firstPointer &&
        cache.Insert("third", 30, 4) != nullptr && cache.GetSize() == 2 && cache.GetByteSize() == 8;

    // Cache identity comes from the frontend's rasterizer request.
    GameEngine::Platform::TextRasterizationRequest firstRequest;
    firstRequest.text = "Cache key";
    firstRequest.fontSize = 12.0f;
    GameEngine::Platform::TextRasterizationRequest nearbyRequest = firstRequest;
    nearbyRequest.fontSize = std::nextafter(firstRequest.fontSize, 13.0f);
    const GameEngine::Rendering::TextRasterizationKey firstKey =
        GameEngine::Rendering::MakeTextRasterizationKey(firstRequest);
    const GameEngine::Rendering::TextRasterizationKey nearbyKey =
        GameEngine::Rendering::MakeTextRasterizationKey(nearbyRequest);
    std::unordered_set<
        GameEngine::Rendering::TextRasterizationKey,
        GameEngine::Rendering::TextRasterizationKeyHash> textKeys;
    textKeys.insert(firstKey);
    textKeys.insert(nearbyKey);

    // The test drives the rasterizer through its platform interface.
    const std::unique_ptr<GameEngine::Platform::ITextRasterizer> textRasterizer =
        TestSupport::CreateTestTextRasterizer();
    const bool rasterizerReady = textRasterizer != nullptr;
    GameEngine::Platform::TextRasterizationRequest normalSpacingRequest;
    normalSpacingRequest.text = "First line\nSecond line";
    normalSpacingRequest.fontSize = 24.0f;
    normalSpacingRequest.maxWidth = 512.0f;
    normalSpacingRequest.lineSpacing = 1.0f;
    GameEngine::Platform::TextRasterizationRequest wideSpacingRequest = normalSpacingRequest;
    wideSpacingRequest.lineSpacing = 2.0f;
    GameEngine::Platform::TextGlyphLayout normalLayout;
    GameEngine::Platform::TextGlyphLayout wideLayout;
    const bool laidOut = rasterizerReady &&
        textRasterizer->LayoutText(normalSpacingRequest, normalLayout) &&
        textRasterizer->LayoutText(wideSpacingRequest, wideLayout);
    const bool lineSpacingChangesRasterizedLayout = laidOut &&
        wideLayout.height > normalLayout.height &&
        !normalLayout.runs.empty() && !normalLayout.runs.front().glyphs.empty() &&
        !wideLayout.runs.empty();

    return Expect(
               rejectsEvictingCurrentFrameEntries && evictsOnlyOlderFrameEntries,
               "frame-bound cache should preserve current-frame results and evict only older entries") &&
        Expect(
            !(firstKey == nearbyKey) && textKeys.size() == 2,
            "text cache keys should preserve every floating-point input bit") &&
        Expect(
            lineSpacingChangesRasterizedLayout,
            "common text rasterization should apply line spacing to glyph layout");
}

/// <summary>
/// 공유 캐시의 재시도와 퇴출이 무효 에셋 처리 및 용량 제한 계약을 지키는지 확인한다.
/// </summary>
bool RunResolvedResourceCacheTests()
{
    using GameEngine::Rendering::ResolvedResourceCache;
    using GameEngine::Rendering::RenderResourceCacheBudget;

    struct FakeResource
    {
        int builtGeneration = 0;
    };

    // Two entries, eight bytes.
    constexpr RenderResourceCacheBudget budget{ 2, 8 };
    ResolvedResourceCache<FakeResource> cache(budget, "test");

    int builds = 0;
    const auto build = [&builds](FakeResource& resource)
    {
        ++builds;
        resource.builtGeneration = builds;
        return true;
    };

    cache.BeginFrame();
    const FakeResource* const first = cache.Resolve(1, 4, build);
    // A second ask in the same frame is a hit, so nothing is built twice.
    const FakeResource* const firstAgain = cache.Resolve(1, 4, build);
    const bool cachesOnHit = first && first == firstAgain && builds == 1;

    // A build that fails is recorded, so a broken resource is not attempted every frame. This is
    // what stops one unreadable asset from costing a rebuild attempt per frame forever.
    int failedAttempts = 0;
    const auto failingBuild = [&failedAttempts](FakeResource&)
    {
        ++failedAttempts;
        return false;
    };
    const bool refusedOnce = cache.Resolve(2, 4, failingBuild) == nullptr;
    const bool notRetriedThisFrame = cache.Resolve(2, 4, failingBuild) == nullptr;
    cache.BeginFrame();
    const bool notRetriedNextFrame = cache.Resolve(2, 4, failingBuild) == nullptr;
    const bool recordsFailure =
        refusedOnce && notRetriedThisFrame && notRetriedNextFrame && failedAttempts == 1;

    // Filling the budget and then asking for one more must turn the request away without
    // building it. Building first and discarding the result is the tempting order, and it is
    // wrong: a full cache turns away every remaining resource in the frame and retries them the
    // next frame, so it would pay for a discarded GPU upload on every one of those frames.
    ResolvedResourceCache<FakeResource> full(budget, "test");
    full.BeginFrame();
    static_cast<void>(full.Resolve(10, 4, build));
    static_cast<void>(full.Resolve(11, 4, build));
    const int buildsBeforeRejection = builds;
    // Both entries were used this frame, so neither can be evicted to make room for this one.
    const bool rejected = full.Resolve(12, 4, build) == nullptr;
    const bool didNotBuildWhenFull = rejected && builds == buildsBeforeRejection;

    // A rejection describes one frame's contention, so the next frame tries again. By then the
    // earlier entries are evictable, so there is room and the build runs.
    full.BeginFrame();
    const bool retriedNextFrame = full.Resolve(12, 4, build) != nullptr &&
        builds == buildsBeforeRejection + 1;

    return Expect(cachesOnHit, "a cached resource should not be built twice") &&
        Expect(recordsFailure, "a build that failed should not be attempted again") &&
        Expect(didNotBuildWhenFull, "a resource that does not fit should not be built at all") &&
        Expect(retriedNextFrame, "a resource turned away for space should be retried next frame");
}

/// <summary>
/// GPU 리소스를 쥔 캐시는 그것을 아직 읽고 있을 수 있는 모든 프레임이 끝날 때까지 항목을
/// 유지한다.
///
/// 렌더러는 CPU가 GPU보다 한 프레임 앞서 달리게 하므로, 지난 프레임에 기록된 command list가
/// 아직 실행 중일 수 있다. 그것이 그리는 항목을 퇴거하면 밑의 리소스를 해제하고 descriptor
/// 슬롯을 재활용하는 셈이 된다. 현재 프레임만 보호하는 것 — CPU만 읽는 캐시에는 그것으로
/// 충분하다 — 으로는 부족하고, 테스트에서 돌 만큼 작은 장면에서는 아무것도 퇴거되지 않으므로,
/// 규칙은 관찰되는 대신 여기서 고정된다.
/// </summary>
bool RunFrameBoundCacheRetentionTests()
{
    using GameEngine::Rendering::FrameBoundCache;

    // Room for two entries, and entries from the last two frames are protected.
    FrameBoundCache<std::string, int> gpuCache(2, 8, 2);

    gpuCache.BeginFrame();
    const bool admitted = gpuCache.MakeRoom(4) && gpuCache.Insert("first", 10, 4) != nullptr;

    gpuCache.BeginFrame();
    const bool admittedSecond = gpuCache.MakeRoom(4) && gpuCache.Insert("second", 20, 4) != nullptr;

    // "first" was used one frame ago. A single-frame rule would let it go; this one must not,
    // because the frame that used it may still be on the GPU. Checked through GetSize rather
    // than Find, because Find marks an entry as used this frame and would extend the very
    // window under test.
    const bool protectsLastFrame = !gpuCache.MakeRoom(4) && gpuCache.GetSize() == 2;

    // Advancing past the retained window makes it evictable, so the cache still makes progress.
    gpuCache.BeginFrame();
    const bool releasesAfterWindow =
        gpuCache.MakeRoom(4) && gpuCache.Find("first") == nullptr;

    // The default is one frame, which is what a cache the CPU alone reads wants.
    FrameBoundCache<std::string, int> cpuCache(2, 8);
    cpuCache.BeginFrame();
    static_cast<void>(cpuCache.MakeRoom(4));
    static_cast<void>(cpuCache.Insert("first", 10, 4));
    cpuCache.BeginFrame();
    static_cast<void>(cpuCache.MakeRoom(4));
    static_cast<void>(cpuCache.Insert("second", 20, 4));
    // Same shape as above, but one frame of protection: last frame's entry may go now.
    const bool defaultReleasesLastFrame =
        cpuCache.MakeRoom(4) && cpuCache.Find("first") == nullptr;

    return Expect(admitted && admittedSecond, "a bounded cache should admit entries up to its budget") &&
        Expect(protectsLastFrame, "a two-frame cache should not evict what last frame used") &&
        Expect(releasesAfterWindow, "an entry older than the window should become evictable") &&
        Expect(
            defaultReleasesLastFrame,
            "the default one-frame cache should still release last frame's entry");
}

/// <summary>
/// 변하지 않은 요청은 배치 캐시 퇴출 후에도 같은 아틀라스 페이지의 같은 자리를 사용해야 한다.
/// 백엔드는 페이지 ID로 GPU 업로드를 캐시하므로 불필요한 재배치는 같은 텍스트의 재업로드를 유발한다.
/// </summary>
bool RunTextImageIdStabilityTests()
{
    GameEngine::Rendering::TextRasterizationCache cache{
        TestSupport::CreateTestTextRasterizer() };

    GameEngine::Platform::TextRasterizationRequest request;
    request.text = "Stable";
    request.fontSize = 16.0f;

    cache.BeginFrame();
    const std::shared_ptr<const GameEngine::Rendering::ShapedText> first = cache.Resolve(request);
    if (!first || first->runs.empty())
    {
        return Expect(false, "the platform text rasterizer should be available to this test");
    }
    const std::uint64_t firstPageId = first->runs.front().page->id;
    const GameEngine::Rendering::TextGlyphQuad firstGlyph = first->runs.front().glyphs->front();

    // Push the shaping cache past its entry budget, one distinct string per frame, so the
    // original entry is the oldest unused one and is evicted. The atlas keeps its glyph slots:
    // eviction forgets the arrangement, not the letters.
    constexpr int FillerCount = 400;
    for (int index = 0; index < FillerCount; ++index)
    {
        cache.BeginFrame();
        GameEngine::Platform::TextRasterizationRequest filler = request;
        filler.text = "Filler " + std::to_string(index);
        static_cast<void>(cache.Resolve(filler));
    }

    cache.BeginFrame();
    const std::shared_ptr<const GameEngine::Rendering::ShapedText> again = cache.Resolve(request);

    return Expect(
               again && again->IsValid() && !again->runs.empty(),
               "an evicted text request should shape again") &&
        Expect(
            again->runs.front().page->id == firstPageId,
            "an unchanged text request should keep its atlas page after eviction") &&
        Expect(
            again->runs.front().glyphs->front().u == firstGlyph.u &&
                again->runs.front().glyphs->front().v == firstGlyph.v,
            "an unchanged text request should reuse the same glyph slots after eviction") &&
        Expect(
            again.get() != first.get(),
            "the eviction this test depends on should actually have happened");
}

/// <summary>
/// 텍스트는 프론트엔드에서 한 번 배치되어 모든 백엔드가 공유한다. 이 검사는 아틀라스가
/// 지켜야 하는 성질들을 고정한다: 바뀌지 않은 문자열은 몇 프레임이 그리든 배치 한 번 비용이고,
/// 다른 문자열은 같은 아틀라스 페이지를 공유하며 — 그것이 아틀라스의 존재 이유다 — 겹치는
/// 글자는 이미 있는 슬롯을 재사용해 페이지 픽셀이 다시 바뀌지 않는다.
/// </summary>
bool RunTextRasterizationCacheTests()
{
    GameEngine::Rendering::TextRasterizationCache cache{
        TestSupport::CreateTestTextRasterizer() };

    GameEngine::Platform::TextRasterizationRequest request;
    request.text = "Cached";
    request.fontSize = 18.0f;

    cache.BeginFrame();
    const std::shared_ptr<const GameEngine::Rendering::ShapedText> first = cache.Resolve(request);
    if (!first || first->runs.empty())
    {
        // A machine without the platform font stack cannot exercise this; say so rather than
        // reporting a pass that proved nothing.
        return Expect(false, "the platform text rasterizer should be available to this test");
    }

    // Resolving the same request again, and again in a later frame, must reuse the shaping.
    const std::shared_ptr<const GameEngine::Rendering::ShapedText> sameFrame =
        cache.Resolve(request);
    cache.BeginFrame();
    const std::shared_ptr<const GameEngine::Rendering::ShapedText> laterFrame =
        cache.Resolve(request);

    // A different string shares the same atlas page, and one made of letters the atlas has
    // already seen adds nothing to it: the page revision stays put.
    const std::uint64_t revisionBeforeReuse = first->runs.front().page->revision;
    GameEngine::Platform::TextRasterizationRequest reuseRequest = request;
    reuseRequest.text = "dehaC";
    const std::shared_ptr<const GameEngine::Rendering::ShapedText> reused =
        cache.Resolve(reuseRequest);
    const bool sharesAtlasPage = reused && !reused->runs.empty() &&
        reused->runs.front().page == first->runs.front().page &&
        reused->runs.front().page->revision == revisionBeforeReuse;

    // A size that differs by one representable step is a different shaping, not a cache hit,
    // and its glyphs are new slots — the page changes.
    GameEngine::Platform::TextRasterizationRequest nearbyRequest = request;
    nearbyRequest.fontSize = std::nextafter(request.fontSize, 19.0f);
    const std::shared_ptr<const GameEngine::Rendering::ShapedText> nearby =
        cache.Resolve(nearbyRequest);
    const bool newSizeAddsGlyphs = nearby && !nearby->runs.empty() &&
        nearby->runs.front().page->revision > revisionBeforeReuse;

    // 글리프는 폰트 크기에 걸맞은 픽셀 크기를 가져야 한다. 래스터화가 조용히 점 몇 개를
    // 내놓으면 배치는 유효해 보이지만 화면에는 부스러기만 남는다 — 그 실패를 여기서 잡는다.
    float tallestGlyph = 0.0f;
    for (const GameEngine::Rendering::TextGlyphQuad& glyph : *first->runs.front().glyphs)
    {
        tallestGlyph = (std::max)(tallestGlyph, glyph.height);
    }
    const bool glyphsAreGlyphSized = tallestGlyph >= request.fontSize * 0.3f;
    if (!glyphsAreGlyphSized)
    {
        std::cerr << "  tallest glyph: " << tallestGlyph << " px for font size "
                  << request.fontSize << ", block " << first->width << "x" << first->height
                  << ", glyphs " << first->runs.front().glyphs->size() << '\n';
    }

    return Expect(
               first->IsValid() && first->runs.front().page->IsValid() &&
                   !first->runs.front().glyphs->empty(),
               "a shaped text should carry a valid page and its glyph placements") &&
        Expect(glyphsAreGlyphSized, "glyph slots should be sized like glyphs, not specks") &&
        Expect(
            sameFrame == first && laterFrame == first,
            "an unchanged request should reuse its shaping across frames") &&
        Expect(sharesAtlasPage, "reordered letters should reuse the atlas without changing it") &&
        Expect(newSizeAddsGlyphs, "a different font size should add new glyphs to the atlas");
}

/// <summary>
/// 아틀라스의 규칙들을 플랫폼 없이 고정한다: 같은 키는 래스터 한 번에 같은 슬롯이고, 새
/// 글리프만 페이지를 바꾸며, 페이지가 차면 다음 페이지가 열리고, 상한에 닿으면 에포크가 오르되
/// 이전 페이지를 쥔 쪽은 계속 유효한 픽셀을 본다.
/// </summary>
bool RunGlyphAtlasTests()
{
    using GameEngine::Rendering::GlyphAtlas;
    using GameEngine::Rendering::GlyphAtlasSlot;
    using GameEngine::Rendering::GlyphKey;

    GlyphAtlas atlas;
    int rasterizations = 0;
    const auto makeRasterize = [&rasterizations](const unsigned int width, const unsigned int height)
    {
        return [&rasterizations, width, height](GameEngine::Platform::RasterizedGlyph& glyph)
        {
            ++rasterizations;
            glyph.width = width;
            glyph.height = height;
            glyph.alphaPixels.assign(static_cast<std::size_t>(width) * height, std::byte{ 200 });
            glyph.offsetX = 1.0f;
            glyph.offsetY = -2.0f;
            return true;
        };
    };

    const GlyphKey keyA{ 1, 0x41000000u, 10 };
    const GlyphAtlasSlot* const slotA = atlas.Resolve(keyA, makeRasterize(8, 12));
    const bool firstSlotFilled = slotA && slotA->hasInk && slotA->page &&
        slotA->width == 8 && slotA->height == 12 && slotA->uWidth > 0.0f &&
        rasterizations == 1;
    const std::uint64_t revisionAfterA = slotA ? slotA->page->revision : 0;

    // 같은 키는 다시 래스터화되지 않고, 페이지도 변하지 않는다.
    const GlyphAtlasSlot* const slotAAgain = atlas.Resolve(keyA, makeRasterize(8, 12));
    const bool reusesSlot = slotAAgain && slotAAgain->hasInk &&
        slotAAgain->page->revision == revisionAfterA && rasterizations == 1;

    // 새 키는 같은 페이지의 새 자리에 들어가고 revision이 오른다.
    const GlyphKey keyB{ 1, 0x41000000u, 11 };
    const GlyphAtlasSlot* const slotB = atlas.Resolve(keyB, makeRasterize(8, 12));
    const bool addsGlyph = slotB && slotB->page == slotAAgain->page &&
        slotB->page->revision > revisionAfterA &&
        (slotB->u != slotAAgain->u || slotB->v != slotAAgain->v);

    // 잉크 없는 글리프는 페이지 없이 성공한다.
    const GlyphKey spaceKey{ 1, 0x41000000u, 12 };
    const GlyphAtlasSlot* const spaceSlot = atlas.Resolve(spaceKey, makeRasterize(0, 0));
    const bool inklessSucceeds = spaceSlot && !spaceSlot->hasInk && !spaceSlot->page;

    // 실패한 래스터화는 기록되어 다시 시도되지 않는다.
    int failures = 0;
    const auto failingRasterize = [&failures](GameEngine::Platform::RasterizedGlyph&)
    {
        ++failures;
        return false;
    };
    const GlyphKey brokenKey{ 1, 0x41000000u, 13 };
    const bool failedOnce = atlas.Resolve(brokenKey, failingRasterize) == nullptr &&
        atlas.Resolve(brokenKey, failingRasterize) == nullptr && failures == 1;

    // 페이지보다 큰 글리프들로 페이지를 채우면 다음 페이지가 열리고, 상한을 넘기면 에포크가
    // 오른다. 이전 페이지를 쥔 쪽은 그 픽셀을 계속 본다.
    const std::shared_ptr<const GameEngine::Rendering::RasterizedTextImage> heldPage =
        slotA->page;
    const std::uint64_t heldPageId = heldPage->id;
    std::uint16_t nextGlyphId = 100;
    const auto fillOnePage = [&atlas, &makeRasterize, &nextGlyphId]
    {
        // 1000x1000 글리프는 한 페이지에 하나만 들어간다.
        const GlyphKey bigKey{ 2, 0x42000000u, nextGlyphId++ };
        return atlas.Resolve(bigKey, makeRasterize(1000, 1000)) != nullptr;
    };
    bool filled = true;
    for (std::size_t page = 0; filled && page < GlyphAtlas::MaximumPages + 1; ++page)
    {
        filled = fillOnePage();
    }
    const bool pagesGrewThenRestarted = filled && atlas.GetEpoch() == 1;

    // 에포크 뒤의 글리프는 새 페이지에 살고, 쥐고 있던 옛 페이지는 그대로다.
    const GlyphAtlasSlot* const slotAfterRestart = atlas.Resolve(keyA, makeRasterize(8, 12));
    const bool restartRebuildsGlyphs = slotAfterRestart && slotAfterRestart->page &&
        slotAfterRestart->page->id != heldPageId &&
        heldPage->id == heldPageId && heldPage->IsValid();

    return Expect(firstSlotFilled, "a first glyph should be rasterized into a page slot") &&
        Expect(reusesSlot, "the same key should reuse its slot without rasterizing again") &&
        Expect(addsGlyph, "a new key should take a new slot and raise the page revision") &&
        Expect(inklessSucceeds, "an inkless glyph should succeed without a page") &&
        Expect(failedOnce, "a failed rasterization should be recorded, not retried") &&
        Expect(pagesGrewThenRestarted, "exhausting the page limit should raise the epoch") &&
        Expect(
            restartRebuildsGlyphs,
            "after a restart glyphs should live on new pages while held pages stay valid");
}

/// <summary>
/// descriptor 풀은 퇴출한 텍스처 바인딩의 슬롯을 재활용해야 한다.
/// 매 프레임 텍스트가 바뀌어도 descriptor가 누적되어 힙이 소진되어서는 안 된다.
/// </summary>
bool RunDescriptorAllocatorTests()
{
    using GameEngine::Rendering::D3D12::D3D12DescriptorAllocation;
    using GameEngine::Rendering::D3D12::D3D12DescriptorIndexAllocator;

    D3D12DescriptorIndexAllocator allocator;
    allocator.Configure(3);

    UINT first = 0;
    UINT second = 0;
    UINT third = 0;
    UINT overflow = 0;
    const bool filled = allocator.TryAllocate(first) && allocator.TryAllocate(second) &&
        allocator.TryAllocate(third);
    const bool rejectedWhenFull = !allocator.HasFreeSlot() && !allocator.TryAllocate(overflow);

    allocator.Release(second);
    UINT recycled = 0;
    const bool reusedReleasedSlot =
        allocator.HasFreeSlot() && allocator.TryAllocate(recycled) && recycled == second;

    // A scoped allocation must return its slot without an explicit release at the eviction site.
    D3D12DescriptorIndexAllocator scopedAllocator;
    scopedAllocator.Configure(2);
    UINT scopedIndex = 0;
    bool heldWhileInScope = false;
    {
        const D3D12DescriptorAllocation scoped = [&scopedAllocator]
        {
            UINT index = 0;
            return scopedAllocator.TryAllocate(index)
                ? D3D12DescriptorAllocation(scopedAllocator, index)
                : D3D12DescriptorAllocation();
        }();
        scopedIndex = scoped.GetIndex();
        heldWhileInScope = scoped.IsValid() && scopedAllocator.GetUsedCount() == 1;
    }
    const bool scopeReturnedSlot = scopedAllocator.GetUsedCount() == 0;
    UINT afterScope = 0;
    const bool reallocatedAfterScope =
        scopedAllocator.TryAllocate(afterScope) && afterScope == scopedIndex;

    // Moving an allocation must transfer ownership rather than release the slot twice.
    D3D12DescriptorAllocation source(scopedAllocator, afterScope);
    {
        const D3D12DescriptorAllocation moved(std::move(source));
        const bool ownershipMoved = moved.IsValid() && !source.IsValid();
        if (!Expect(ownershipMoved, "moving a descriptor allocation should leave the source empty"))
        {
            return false;
        }
    }
    const bool movedDestinationReleasedOnce = scopedAllocator.GetUsedCount() == 0;

    return Expect(
               filled && first != second && second != third,
               "a descriptor pool should hand out distinct slots up to its capacity") &&
        Expect(rejectedWhenFull, "a full descriptor pool should reject further allocations") &&
        Expect(reusedReleasedSlot, "a released descriptor slot should be reused") &&
        Expect(
            heldWhileInScope && scopeReturnedSlot && reallocatedAfterScope,
            "a scoped descriptor allocation should return its slot on destruction") &&
        Expect(
            movedDestinationReleasedOnce,
            "a moved-to descriptor allocation should release its slot exactly once");
}

/// <summary>
/// GPU 상주 예산이 공용 정책에서 와서 D3D12 백엔드까지 닿는 길이다.
///
/// 그 예산이 백엔드의 파일 로컬 상수이면, 정책 헤더가 백엔드마다 다른 상한을 없애려고 있는데도
/// 정작 GPU에 무엇이 얼마나 상주하는지는 백엔드가 혼자 정하게 된다.
/// 여기서 확인하는 것은 세 가지다: 정책이 두 백엔드에 같은 상주량을 뜻하는지, 정책과 장치 한계를
/// 결합하는 규칙이 언제나 작은 쪽을 고르는지, 그리고 그렇게 좁혀진 수가 캐시의 실제 퇴거 지점을
/// 옮기는지.
/// </summary>
bool RunTextureBindingBudgetTests()
{
    using GameEngine::Rendering::TextCacheBudget;
    using GameEngine::Rendering::TextureBindingBudget;
    using GameEngine::Rendering::TextureCacheBudget;
    using GameEngine::Rendering::D3D12::CombineDescriptorCount;

    // D3D11은 텍스처와 텍스트를 각자의 리졸버 캐시에 GPU로 상주시키고, D3D12는 그 둘을 바인딩
    // 캐시 하나에 함께 상주시킨다. 합이라야 같은 장면이 두 백엔드에서 같은 만큼 상주한다.
    const bool bindingBudgetIsTheSumOfTheResolverBudgets =
        TextureBindingBudget.maximumBytes ==
            TextureCacheBudget.maximumBytes + TextCacheBudget.maximumBytes &&
        TextureBindingBudget.maximumEntries ==
            TextureCacheBudget.maximumEntries + TextCacheBudget.maximumEntries;

    const bool takesThePolicyWhenTheDeviceIsRoomier = CombineDescriptorCount(64, 1024) == 64;
    const bool takesTheDeviceWhenThePolicyOverreaches = CombineDescriptorCount(4096, 1024) == 1024;
    const bool agreesWhenBothAreEqual = CombineDescriptorCount(1024, 1024) == 1024;
    const bool realPolicyFitsARoomyDevice =
        CombineDescriptorCount(TextureBindingBudget.maximumEntries, 1'000'000) ==
        static_cast<unsigned int>(TextureBindingBudget.maximumEntries);

    // 업로더가 하는 일과 같다: 정책이 원한 수로 캐시를 만들고, 힙이 실제로 준 수로 좁힌다. 좁히지
    // 않은 캐시와 나란히 두면 좁힌 수가 퇴거 지점을 실제로 옮겼다는 것이 보인다 — 두 캐시가 같은
    // 두 항목을 받고, 세 번째에서만 갈린다.
    const auto fillTwoAndAskForAThird = [](const std::size_t narrowedTo)
    {
        GameEngine::Rendering::FrameBoundCache<int, int> bindings(
            TextureBindingBudget.maximumEntries, TextureBindingBudget.maximumBytes);
        if (narrowedTo != 0)
        {
            bindings.SetMaximumEntries(narrowedTo);
        }
        bindings.BeginFrame();
        const bool filled = bindings.MakeRoom(1) && bindings.Insert(1, 10, 1) != nullptr &&
            bindings.MakeRoom(1) && bindings.Insert(2, 20, 1) != nullptr;
        // 이번 프레임에 쓴 항목은 퇴거될 수 없으므로, 상한에 닿았다면 세 번째는 자리를 얻지 못한다.
        return std::pair{ filled, bindings.MakeRoom(1) };
    };
    const auto [policyFilled, policyAdmittedAThird] = fillTwoAndAskForAThird(0);
    const auto [narrowedFilled, narrowedAdmittedAThird] =
        fillTwoAndAskForAThird(CombineDescriptorCount(TextureBindingBudget.maximumEntries, 2));

    return Expect(
               bindingBudgetIsTheSumOfTheResolverBudgets,
               "the GPU binding budget should cover what the two resolver budgets cover") &&
        Expect(
            takesThePolicyWhenTheDeviceIsRoomier && takesTheDeviceWhenThePolicyOverreaches &&
                agreesWhenBothAreEqual && realPolicyFitsARoomyDevice,
            "the descriptor count should be the smaller of the policy and the device limit") &&
        Expect(
            policyFilled && narrowedFilled && policyAdmittedAThird && !narrowedAdmittedAThird,
            "narrowing the bound to the device limit should move where the cache stops admitting");
}

static const TestSupport::Registration gRenderResourceCacheTests{
    "RenderCache", "render resource cache tests should pass", RunRenderResourceCacheTests };

static const TestSupport::Registration gResolvedResourceCacheTests{
    "RenderCache", "resolved resource cache tests should pass", RunResolvedResourceCacheTests };

static const TestSupport::Registration gFrameBoundCacheRetentionTests{
    "RenderCache", "frame-bound cache retention tests should pass", RunFrameBoundCacheRetentionTests };

static const TestSupport::Registration gTextImageIdStabilityTests{
    "RenderCache", "text image id stability tests should pass", RunTextImageIdStabilityTests };

static const TestSupport::Registration gTextRasterizationCacheTests{
    "RenderCache", "text rasterization cache tests should pass", RunTextRasterizationCacheTests };

static const TestSupport::Registration gGlyphAtlasTests{
    "RenderCache", "glyph atlas tests should pass", RunGlyphAtlasTests };

static const TestSupport::Registration gDescriptorAllocatorTests{
    "RenderCache", "descriptor allocator tests should pass", RunDescriptorAllocatorTests };

static const TestSupport::Registration gTextureBindingBudgetTests{
    "RenderCache", "texture binding budget tests should pass", RunTextureBindingBudgetTests };
