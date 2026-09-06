#include "TextPageUploadTests.h"

#include <cstddef>
#include <cstdint>
#include <optional>

#include "Rendering/RenderFrame.h"
#include "Rendering/TextPageUpload.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>글리프 하나가 더해진 페이지다: 픽셀이 늘고 revision이 오른다.</summary>
    void AddGlyph(GameEngine::Rendering::RasterizedTextImage& page, const std::byte ink)
    {
        page.alphaPixels[0] = ink;
        ++page.revision;
    }
}

bool RunTextPageUploadTests()
{
    namespace Rendering = GameEngine::Rendering;

    Rendering::RasterizedTextImage page;
    page.id = 7;
    page.revision = 1;
    page.width = 4;
    page.height = 4;
    page.alphaPixels.assign(16, std::byte{ 0 });

    // 아직 아무것도 올리지 않았으면 올릴 것이 있다.
    const std::optional<Rendering::TextPageUpload> first = Rendering::PlanTextPageUpload(page, 0);
    const bool firstUploadPlanned = first && first->revision == 1;

    // 올린 그대로면 올릴 것이 없다. 이것이 프레임마다 1MB를 다시 올리지 않는 이유다.
    const bool unchangedPageIsSkipped =
        !Rendering::PlanTextPageUpload(page, first ? first->revision : 0);

    // 글리프가 더해지면 다시 올린다.
    AddGlyph(page, std::byte{ 200 });
    const std::optional<Rendering::TextPageUpload> second =
        Rendering::PlanTextPageUpload(page, first ? first->revision : 0);
    const bool addedGlyphIsUploaded = second && second->revision == 2;

    // 🔴 업로드 도중에 글리프가 더해진 경우다. 계획을 받은 뒤 픽셀을 읽는 사이에 게임 스레드가
    // 페이지를 키운 모습이며, 기록해야 할 값은 계획이 준 것이지 지금의 revision이 아니다.
    // 계획이 준 값을 기록하면 자란 만큼이 다음 계획에 남고, 지금 값을 기록하면 사라진다.
    AddGlyph(page, std::byte{ 201 });
    const bool growthDuringUploadSurvives =
        Rendering::PlanTextPageUpload(page, second ? second->revision : 0).has_value();
    const bool growthDuringUploadWouldBeLost =
        !Rendering::PlanTextPageUpload(page, page.revision).has_value();

    return Expect(firstUploadPlanned, "a page never uploaded should be uploaded") &&
        Expect(unchangedPageIsSkipped, "a page already uploaded should not be uploaded again") &&
        Expect(addedGlyphIsUploaded, "a page that gained a glyph should be uploaded again") &&
        Expect(
            growthDuringUploadSurvives,
            "a glyph added during an upload should still be waiting in the next plan") &&
        Expect(
            growthDuringUploadWouldBeLost,
            "recording the revision read after the pixels would lose that glyph forever");
}

static const TestSupport::Registration gTextPageUploadTests{
    "RenderCache", "text page upload tests should pass", RunTextPageUploadTests };
