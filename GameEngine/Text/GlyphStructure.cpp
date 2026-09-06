#include "pch.h"
#include "GlyphStructure.h"

#include <algorithm>
#include <vector>

namespace GameEngine::Text
{

namespace
{
    /// <summary>
    /// 한 줄에서 잉크가 이어지는 구간들의 길이를 모은다.
    ///
    /// 「획 하나의 두께」를 그 줄에서 잰 값이 이 길이다. 줄마다 여럿 나올 수 있고 — 획이 여러
    /// 개면 — 그것들을 다 모아 나중에 중심값을 취한다.
    /// </summary>
    void CollectRuns(
        const std::span<const std::byte> pixels,
        const std::size_t start,
        const std::size_t step,
        const unsigned int count,
        const unsigned char threshold,
        std::vector<float>& runs)
    {
        unsigned int current = 0;
        for (unsigned int index = 0; index < count; ++index)
        {
            const std::size_t at = start + static_cast<std::size_t>(index) * step;
            if (at >= pixels.size())
            {
                break;
            }
            if (static_cast<unsigned char>(pixels[at]) > threshold)
            {
                ++current;
                continue;
            }
            if (current > 0)
            {
                runs.push_back(static_cast<float>(current));
                current = 0;
            }
        }
        if (current > 0)
        {
            runs.push_back(static_cast<float>(current));
        }
    }

    /// <summary>중심값이다. 비어 있으면 0이다.</summary>
    [[nodiscard]] float Median(std::vector<float>& values)
    {
        if (values.empty())
        {
            return 0.0f;
        }
        const std::size_t middle = values.size() / 2;
        std::ranges::nth_element(values, values.begin() + static_cast<std::ptrdiff_t>(middle));
        const float upper = values[middle];
        if (values.size() % 2 == 1)
        {
            return upper;
        }
        // 짝수 개면 가운데 둘의 평균이다. 아래쪽 값은 위 호출이 이미 앞쪽으로 갈라 두었다.
        const auto lower = *std::ranges::max_element(
            values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
        return (lower + upper) * 0.5f;
    }
}

GlyphStructure MeasureGlyphStructure(
    const std::span<const std::byte> alphaPixels,
    const unsigned int width,
    const unsigned int height,
    const float originX,
    const float originY,
    const unsigned char inkThreshold)
{
    GlyphStructure structure;
    if (width == 0 || height == 0 || alphaPixels.empty())
    {
        return structure;
    }

    unsigned int minX = width;
    unsigned int minY = height;
    unsigned int maxX = 0;
    unsigned int maxY = 0;
    bool any = false;
    double area = 0.0;

    for (unsigned int y = 0; y < height; ++y)
    {
        for (unsigned int x = 0; x < width; ++x)
        {
            const std::size_t at = static_cast<std::size_t>(y) * width + x;
            if (at >= alphaPixels.size())
            {
                break;
            }
            const auto value = static_cast<unsigned char>(alphaPixels[at]);
            // 넓이는 옅은 가장자리까지 <b>비율대로</b> 센다. 경계는 문턱으로 자르지만 넓이까지
            // 자르면 안티에일리어싱이 만든 잉크가 통째로 사라져 두 래스터라이저를 견줄 수 없다.
            area += static_cast<double>(value) / 255.0;
            if (value <= inkThreshold)
            {
                continue;
            }
            minX = (std::min)(minX, x);
            minY = (std::min)(minY, y);
            maxX = (std::max)(maxX, x);
            maxY = (std::max)(maxY, y);
            any = true;
        }
    }

    structure.inkArea = area;
    if (!any)
    {
        return structure;
    }

    structure.left = originX + static_cast<float>(minX);
    structure.top = originY + static_cast<float>(minY);
    structure.right = originX + static_cast<float>(maxX) + 1.0f;
    structure.bottom = originY + static_cast<float>(maxY) + 1.0f;

    std::vector<float> horizontal;
    for (unsigned int y = minY; y <= maxY; ++y)
    {
        CollectRuns(
            alphaPixels, static_cast<std::size_t>(y) * width, 1, width, inkThreshold, horizontal);
    }
    std::vector<float> vertical;
    for (unsigned int x = minX; x <= maxX; ++x)
    {
        CollectRuns(alphaPixels, x, width, height, inkThreshold, vertical);
    }
    structure.strokeWidth = Median(horizontal);
    structure.strokeHeight = Median(vertical);
    return structure;
}

GlyphStructure MeasureGlyphStructure(
    const CoverageBitmap& bitmap, const unsigned char inkThreshold)
{
    return MeasureGlyphStructure(
        bitmap.alpha, bitmap.width, bitmap.height, bitmap.originX, bitmap.originY, inkThreshold);
}

}
