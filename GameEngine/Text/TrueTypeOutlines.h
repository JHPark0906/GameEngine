#pragma once

#include <cstdint>

#include "FontFace.h"
#include "GlyphOutline.h"

namespace GameEngine::Text
{

/// <summary>
/// TrueType의 glyf에서 글리프 하나의 윤곽선을 읽는다.
/// 단순 글리프와 합성 글리프를 모두 지원한다. 합성 글리프는 여러 부품과 변환으로 정의되며,
/// 한글 음절처럼 부품을 조립하는 폰트를 읽으려면 이 경로도 필요하다.
/// </summary>
/// <param name="face">읽을 face다. TrueType 윤곽선을 가진 것이어야 한다.</param>
/// <param name="glyphId">face 안의 글리프 번호다.</param>
/// <param name="outline">윤곽선을 받는다. 잉크가 없는 글리프는 비어 있고 그것도 성공이다.</param>
/// <returns>읽었으면 true다. 파일이 어긋나 있으면 false다.</returns>
[[nodiscard]] bool GetTrueTypeGlyphOutline(
    const FontFace& face, std::uint16_t glyphId, GlyphOutline& outline);

}
