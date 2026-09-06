#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "../Platform/ITextRasterizer.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 텍스트 배치 요청의 정확한 캐시 정체성이다. float 입력은 비트 패턴으로 비교하므로, 두
/// 요청이 동일한 배치를 만들 때만 캐시된 결과를 공유한다.
/// </summary>
struct TextRasterizationKey
{
    std::string text;
    std::string fontFamily;
    int fontWeight = 400;
    std::uint32_t fontSizeBits = 0;
    std::uint32_t maxWidthBits = 0;
    std::uint32_t lineSpacingBits = 0;
    Platform::TextAlignment alignment = Platform::TextAlignment::Left;

    [[nodiscard]] bool operator==(const TextRasterizationKey&) const = default;
};

/// <summary>TextRasterizationKey를 해시 컨테이너의 키로 쓰기 위한 해시이다.</summary>
struct TextRasterizationKeyHash
{
    [[nodiscard]] std::size_t operator()(const TextRasterizationKey& key) const;
};

/// <summary>래스터화 요청의 백엔드 독립적 캐시 정체성을 만든다.</summary>
[[nodiscard]] TextRasterizationKey MakeTextRasterizationKey(
    const Platform::TextRasterizationRequest& request);

}
