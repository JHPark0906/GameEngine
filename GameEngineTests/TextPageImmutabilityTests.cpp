#include "TextPageImmutabilityTests.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Platform/ITextRasterizer.h"
#include "Rendering/GlyphAtlas.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>글리프마다 자기 번호로 칠하는 래스터라이저다. 크기는 슬롯이 겹치지 않게 다르다.</summary>
    [[nodiscard]] auto MakeRasterize(const std::uint16_t glyphId)
    {
        return [glyphId](GameEngine::Platform::RasterizedGlyph& glyph)
        {
            glyph.width = 6u + glyphId % 11u;
            glyph.height = 5u + glyphId % 7u;
            glyph.alphaPixels.assign(
                static_cast<std::size_t>(glyph.width) * glyph.height,
                static_cast<std::byte>(1 + glyphId % 254));
            glyph.offsetX = 0.0f;
            glyph.offsetY = -static_cast<float>(glyph.height);
            return true;
        };
    }

    [[nodiscard]] GameEngine::Rendering::GlyphKey KeyOf(const std::uint16_t glyphId)
    {
        return GameEngine::Rendering::GlyphKey{ 1, 0x41000000u, glyphId };
    }
}

bool RunTextPageImmutabilityTests()
{
    namespace Rendering = GameEngine::Rendering;

    Rendering::GlyphAtlas atlas;

    // 한 글리프를 넣고 그 페이지를 받아 둔다. 프레임이 하는 일이 이것이다: 포인터를 복사해
    // 가지고 가서, 게임 스레드가 다음 프레임을 만드는 동안 그것을 읽는다.
    const Rendering::GlyphAtlasSlot* const first = atlas.Resolve(KeyOf(10), MakeRasterize(10));
    if (!Expect(first != nullptr && first->page != nullptr, "the first glyph should take a slot"))
    {
        return false;
    }
    const std::shared_ptr<const Rendering::RasterizedTextImage> carried = first->page;
    const std::vector<std::byte> bytesWhenCarried = carried->alphaPixels;
    const std::uint64_t revisionWhenCarried = carried->revision;
    const std::uint64_t idWhenCarried = carried->id;

    // 🔴 그 뒤로 글리프를 더 넣는다. 받아 간 그림은 바이트 하나까지 그대로여야 한다.
    for (std::uint16_t glyphId = 11; glyphId < 40; ++glyphId)
    {
        static_cast<void>(atlas.Resolve(KeyOf(glyphId), MakeRasterize(glyphId)));
    }
    const bool carriedPageNeverChanged = carried->alphaPixels == bytesWhenCarried &&
        carried->revision == revisionWhenCarried && carried->id == idWhenCarried;

    // 새 글리프들은 같은 논리적 페이지의 다음 판에 있다: id는 같고 revision은 올랐다. id가
    // 같아야 백엔드가 새 텍스처를 만드는 대신 제자리에 다시 올린다.
    const Rendering::GlyphAtlasSlot* const latest = atlas.Resolve(KeyOf(39), MakeRasterize(39));
    const bool grewIntoTheNextEdition = latest != nullptr && latest->page != nullptr &&
        latest->page != carried && latest->page->id == idWhenCarried &&
        latest->page->revision > revisionWhenCarried;

    // 옛 판을 가리키던 슬롯도 새 판으로 옮겨져 있어야 한다. 아니면 한 id 아래 두 그림이 같은
    // 프레임에 실려, 백엔드가 그 둘을 번갈아 올린다.
    const Rendering::GlyphAtlasSlot* const firstAgain = atlas.Resolve(KeyOf(10), MakeRasterize(10));
    const bool everySlotPointsAtTheNewestEdition =
        firstAgain != nullptr && firstAgain->page == latest->page;

    // 🔴 예열이 끝나면 복사가 0이다. 같은 글리프들을 다시 요청하는 것은 프레임마다 일어나는
    // 일이고, 그때 새 그림이 만들어지면 이 설계를 고른 이유가 사라진다.
    const std::uint64_t snapshotsAfterWarmUp = atlas.GetSnapshotCount();
    const std::uint64_t generationAfterWarmUp = atlas.GetGeneration();
    for (int frame = 0; frame < 10; ++frame)
    {
        for (std::uint16_t glyphId = 10; glyphId < 40; ++glyphId)
        {
            static_cast<void>(atlas.Resolve(KeyOf(glyphId), MakeRasterize(glyphId)));
        }
    }
    const bool steadyStateCopiesNothing =
        atlas.GetSnapshotCount() == snapshotsAfterWarmUp &&
        atlas.GetGeneration() == generationAfterWarmUp;

    // 자랄 때의 비용이다. 단언하지 않고 적는다 — 기계마다 다르고, 나중에 다른 안과 견줄 때
    // 필요한 것은 합격/불합격이 아니라 수치다.
    {
        Rendering::GlyphAtlas measured;
        const auto started = std::chrono::steady_clock::now();
        constexpr int Glyphs = 200;
        for (std::uint16_t glyphId = 0; glyphId < Glyphs; ++glyphId)
        {
            static_cast<void>(measured.Resolve(KeyOf(glyphId), MakeRasterize(glyphId)));
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started);
        const std::size_t pageBytes =
            static_cast<std::size_t>(Rendering::GlyphAtlas::PageSize) *
            Rendering::GlyphAtlas::PageSize;
        std::cout << "  growth cost: page=" << pageBytes / 1024 << " KiB, snapshots="
                  << measured.GetSnapshotCount() << " for " << Glyphs << " glyphs, total="
                  << elapsed.count() << " us, per glyph="
                  << elapsed.count() / Glyphs << " us\n";
    }

    return Expect(
            carriedPageNeverChanged,
            "a page a frame carried should not change after later glyphs are added") &&
        Expect(
            grewIntoTheNextEdition,
            "a later glyph should live in a new edition of the same page id") &&
        Expect(
            everySlotPointsAtTheNewestEdition,
            "an earlier glyph should be re-pointed at the newest edition") &&
        Expect(
            steadyStateCopiesNothing,
            "asking for glyphs that are already there should copy nothing");
}

static const TestSupport::Registration gTextPageImmutabilityTests{
    "RenderCache", "text page immutability tests should pass", RunTextPageImmutabilityTests };
