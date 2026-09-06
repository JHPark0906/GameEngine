#include "pch.h"
#include "TextRasterizationKey.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace GameEngine::Rendering
{

namespace
{
    void HashCombine(std::size_t& seed, const std::size_t value)
    {
        seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

}

std::size_t TextRasterizationKeyHash::operator()(const TextRasterizationKey& key) const
{
    std::size_t result = std::hash<std::string>{}(key.text);
    HashCombine(result, std::hash<std::string>{}(key.fontFamily));
    HashCombine(result, std::hash<int>{}(key.fontWeight));
    HashCombine(result, std::hash<std::uint32_t>{}(key.fontSizeBits));
    HashCombine(result, std::hash<std::uint32_t>{}(key.maxWidthBits));
    HashCombine(result, std::hash<std::uint32_t>{}(key.lineSpacingBits));
    HashCombine(result, std::hash<unsigned char>{}(static_cast<unsigned char>(key.alignment)));
    return result;
}

TextRasterizationKey MakeTextRasterizationKey(const Platform::TextRasterizationRequest& request)
{
    return {
        request.text,
        request.fontFamily,
        request.fontWeight,
        std::bit_cast<std::uint32_t>(request.fontSize),
        std::bit_cast<std::uint32_t>(request.maxWidth),
        std::bit_cast<std::uint32_t>(request.lineSpacing),
        request.alignment
    };
}

}
