#pragma once

#include <cstdint>
#include <optional>

namespace GameEngine::Rendering
{

struct RasterizedTextImage;

/// <summary>
/// 아틀라스 페이지를 한 번 올릴 계획이다. 올릴 픽셀은 페이지가 그대로 가지고 있으므로, 계획이
/// 나르는 것은 <b>그 픽셀과 함께 기록해야 할 revision</b> 하나다.
/// </summary>
struct TextPageUpload
{
    /// <summary>이 업로드를 마친 뒤 "여기까지 올렸다"로 기록할 값이다.</summary>
    std::uint64_t revision = 0;
};

/// <summary>
/// 이 페이지를 다시 올려야 하는지 답하고, 올린다면 기록할 revision을 함께 준다.
/// 올릴 것이 없으면 빈 값이다.
///
/// 발행된 아틀라스 페이지는 불변 스냅샷이다. 호출자는 이 계획과 같은 페이지의 픽셀을 올린 뒤
/// 계획에 담긴 revision을 기록해야 GPU의 픽셀과 업로드 기록이 일치한다.
/// 페이지 id가 같아도 다음 revision은 별도 객체일 수 있다.
/// </summary>
/// <param name="page">올릴 아틀라스 페이지다.</param>
/// <param name="uploadedRevision">이 텍스처에 마지막으로 올린 픽셀의 revision이다.</param>
/// <returns>올릴 것이 있으면 그 계획, 없으면 빈 값이다.</returns>
[[nodiscard]] std::optional<TextPageUpload> PlanTextPageUpload(
    const RasterizedTextImage& page, std::uint64_t uploadedRevision);

}
