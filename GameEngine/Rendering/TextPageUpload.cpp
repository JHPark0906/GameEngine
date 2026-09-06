#include "pch.h"
#include "TextPageUpload.h"

#include "RenderFrame.h"

namespace GameEngine::Rendering
{

std::optional<TextPageUpload> PlanTextPageUpload(
    const RasterizedTextImage& page, const std::uint64_t uploadedRevision)
{
    // 한 번만 읽는다. 두 번 읽으면 두 값이 다를 수 있고, 그 차이가 곧 올라가지 않은 글리프다.
    const std::uint64_t revision = page.revision;
    if (revision == uploadedRevision)
    {
        return std::nullopt;
    }
    return TextPageUpload{ revision };
}

}
