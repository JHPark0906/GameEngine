#include "pch.h"
#include "GlyphRasterizer.h"

#include <algorithm>
#include <cmath>

namespace GameEngine::Text
{

namespace
{
    /// <summary>평탄화된 윤곽선의 한 변이다. 좌표는 이미 픽셀이고 y는 아래로 간다.</summary>
    struct Edge
    {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        /// <summary>위로 가는 변이 +1, 아래로 가는 변이 -1이다. 감김수의 부호가 여기서 온다.</summary>
        int winding = 0;
    };

    /// <summary>
    /// 3차 곡선을 직선으로 쪼갠다.
    ///
    /// 쪼개는 수를 곡선의 <b>휘어진 정도와 화면에서의 크기</b>로 정한다. 고정된 수로 쪼개면 큰
    /// 글자에서는 각지고 작은 글자에서는 헛일을 한다 — 아틀라스가 글리프를 크기마다 굽기 때문에
    /// 작은 크기가 압도적으로 많고, 거기서 낭비하면 그대로 시작 비용이 된다.
    /// </summary>
    [[nodiscard]] unsigned int StepsFor(
        const OutlinePoint start, const OutlineSegment& segment, const float pixelsPerUnit)
    {
        // 제어점이 양 끝을 잇는 직선에서 얼마나 벗어나는지가 휘어진 정도다. 정확한 거리 대신
        // 좌표 차의 합을 쓰는 것은, 여기서 필요한 것이 크기의 어림이지 값 자체가 아니어서다.
        const float spread =
            std::abs(segment.control1.x - start.x) + std::abs(segment.control1.y - start.y) +
            std::abs(segment.control2.x - segment.end.x) +
            std::abs(segment.control2.y - segment.end.y);
        const float pixels = spread * pixelsPerUnit;
        const auto steps = static_cast<unsigned int>(std::sqrt(pixels) * 2.0f);
        return std::clamp(steps, 2u, 64u);
    }

    void AddEdge(
        std::vector<Edge>& edges, const float x0, const float y0, const float x1, const float y1)
    {
        if (y0 == y1)
        {
            // 수평인 변은 주사선을 가로지르지 않으므로 감김수에 아무것도 보태지 않는다.
            return;
        }
        edges.push_back(Edge{ x0, y0, x1, y1, y1 > y0 ? 1 : -1 });
    }

    /// <summary>윤곽선을 픽셀 공간의 변 목록으로 만든다. 폰트의 y는 위로, 픽셀의 y는 아래로 간다.</summary>
    void BuildEdges(
        const GlyphOutline& outline,
        const float pixelsPerUnit,
        const float originX,
        const float originY,
        std::vector<Edge>& edges)
    {
        const auto toDevice = [&](const OutlinePoint point)
        {
            return OutlinePoint{ point.x * pixelsPerUnit - originX,
                                 originY - point.y * pixelsPerUnit };
        };

        for (const OutlineContour& contour : outline.contours)
        {
            OutlinePoint start = contour.start;
            for (const OutlineSegment& segment : contour.segments)
            {
                const unsigned int steps = StepsFor(start, segment, pixelsPerUnit);
                OutlinePoint previous = toDevice(start);
                for (unsigned int step = 1; step <= steps; ++step)
                {
                    const float t = static_cast<float>(step) / static_cast<float>(steps);
                    const float inverse = 1.0f - t;
                    // 3차 베지에를 그대로 편다. 도막이 언제나 3차라서 갈래가 하나다.
                    const float weightStart = inverse * inverse * inverse;
                    const float weightControl1 = 3.0f * inverse * inverse * t;
                    const float weightControl2 = 3.0f * inverse * t * t;
                    const float weightEnd = t * t * t;
                    const OutlinePoint point{
                        weightStart * start.x + weightControl1 * segment.control1.x +
                            weightControl2 * segment.control2.x + weightEnd * segment.end.x,
                        weightStart * start.y + weightControl1 * segment.control1.y +
                            weightControl2 * segment.control2.y + weightEnd * segment.end.y };
                    const OutlinePoint device = toDevice(point);
                    AddEdge(edges, previous.x, previous.y, device.x, device.y);
                    previous = device;
                }
                start = segment.end;
            }
        }
    }

    /// <summary>구간 [left, right)를 가로로 걸친 넓이 그대로 더한다.</summary>
    void AccumulateSpan(
        std::vector<float>& row, const float left, const float right, const float weight)
    {
        const auto width = static_cast<float>(row.size());
        const float from = std::clamp(left, 0.0f, width);
        const float to = std::clamp(right, 0.0f, width);
        if (to <= from)
        {
            return;
        }
        const auto firstPixel = static_cast<std::size_t>(from);
        const auto lastPixel = static_cast<std::size_t>(to);
        if (firstPixel == lastPixel)
        {
            if (firstPixel < row.size())
            {
                row[firstPixel] += weight * (to - from);
            }
            return;
        }
        if (firstPixel < row.size())
        {
            row[firstPixel] += weight * (static_cast<float>(firstPixel + 1) - from);
        }
        for (std::size_t pixel = firstPixel + 1; pixel < lastPixel && pixel < row.size(); ++pixel)
        {
            row[pixel] += weight;
        }
        if (lastPixel < row.size())
        {
            row[lastPixel] += weight * (to - static_cast<float>(lastPixel));
        }
    }

    /// <summary>구간을 픽셀 안 표본 격자로 센다. 초과표본 방식이 쓰는 셈법이다.</summary>
    void AccumulateSampled(
        std::vector<float>& row,
        const float left,
        const float right,
        const float weight,
        const unsigned int horizontalSamples)
    {
        const auto samples = static_cast<float>(horizontalSamples);
        const float perSample = weight / samples;
        for (std::size_t pixel = 0; pixel < row.size(); ++pixel)
        {
            for (unsigned int sample = 0; sample < horizontalSamples; ++sample)
            {
                const float x = static_cast<float>(pixel) +
                    (static_cast<float>(sample) + 0.5f) / samples;
                if (x >= left && x < right)
                {
                    row[pixel] += perSample;
                }
            }
        }
    }

    struct Crossing
    {
        float x = 0.0f;
        int winding = 0;
    };
}

bool RasterizeGlyphOutline(
    const GlyphOutline& outline,
    const float pixelsPerUnit,
    const RasterizerSettings& settings,
    CoverageBitmap& bitmap)
{
    bitmap = CoverageBitmap{};
    if (outline.IsEmpty() || pixelsPerUnit <= 0.0f)
    {
        // 잉크가 없는 글리프다. 공백이 그렇고, 실패가 아니다.
        return true;
    }

    // 윤곽선이 차지하는 자리를 먼저 잰다. 제어점까지 넣으므로 곡선을 반드시 품는다.
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool any = false;
    const auto include = [&](const OutlinePoint point)
    {
        if (!any)
        {
            minX = maxX = point.x;
            minY = maxY = point.y;
            any = true;
            return;
        }
        minX = (std::min)(minX, point.x);
        minY = (std::min)(minY, point.y);
        maxX = (std::max)(maxX, point.x);
        maxY = (std::max)(maxY, point.y);
    };
    for (const OutlineContour& contour : outline.contours)
    {
        include(contour.start);
        for (const OutlineSegment& segment : contour.segments)
        {
            include(segment.control1);
            include(segment.control2);
            include(segment.end);
        }
    }
    if (!any)
    {
        return true;
    }

    // 픽셀 격자에 맞춰 한 칸씩 넉넉히 잡는다. 곡선이 제어점 상자 안에 있으므로 이 여유면 잘리지
    // 않고, 남는 자리는 커버리지 0이라 아틀라스에서만 조금 손해다.
    const float left = std::floor(minX * pixelsPerUnit) - 1.0f;
    const float top = std::floor(-maxY * pixelsPerUnit) - 1.0f;
    const float right = std::ceil(maxX * pixelsPerUnit) + 1.0f;
    const float bottom = std::ceil(-minY * pixelsPerUnit) + 1.0f;
    const auto width = static_cast<unsigned int>(right - left);
    const auto height = static_cast<unsigned int>(bottom - top);
    if (width == 0 || height == 0 || width > 4096 || height > 4096)
    {
        return false;
    }

    std::vector<Edge> edges;
    BuildEdges(outline, pixelsPerUnit, left, -top, edges);
    if (edges.empty())
    {
        return true;
    }

    const unsigned int verticalSamples = (std::max)(1u, settings.verticalSamples);
    const float sampleWeight = 1.0f / static_cast<float>(verticalSamples);

    bitmap.alpha.assign(static_cast<std::size_t>(width) * height, std::byte{ 0 });
    bitmap.width = width;
    bitmap.height = height;
    bitmap.originX = left;
    bitmap.originY = top;

    std::vector<float> row(width, 0.0f);
    std::vector<Crossing> crossings;
    for (unsigned int y = 0; y < height; ++y)
    {
        std::fill(row.begin(), row.end(), 0.0f);
        for (unsigned int sample = 0; sample < verticalSamples; ++sample)
        {
            const float scanY = static_cast<float>(y) +
                (static_cast<float>(sample) + 0.5f) / static_cast<float>(verticalSamples);

            crossings.clear();
            for (const Edge& edge : edges)
            {
                const float lowY = (std::min)(edge.y0, edge.y1);
                const float highY = (std::max)(edge.y0, edge.y1);
                // 반열린 구간으로 판정한다. 두 변이 만나는 꼭짓점을 두 번 세면 그 자리에
                // 실오라기 같은 구멍이 생긴다.
                if (scanY < lowY || scanY >= highY)
                {
                    continue;
                }
                const float t = (scanY - edge.y0) / (edge.y1 - edge.y0);
                crossings.push_back(Crossing{ edge.x0 + t * (edge.x1 - edge.x0), edge.winding });
            }
            if (crossings.size() < 2)
            {
                continue;
            }
            std::sort(
                crossings.begin(), crossings.end(),
                [](const Crossing& a, const Crossing& b) { return a.x < b.x; });

            // 0이 아닌 감김수로 채운다. 바깥 고리와 안쪽 고리가 반대로 감겨 있으므로, 이 규칙이
            // 글자 안의 구멍을 비운다 — 홀짝 규칙을 쓰면 겹친 획이 뚫린다.
            int winding = 0;
            float spanStart = 0.0f;
            for (const Crossing& crossing : crossings)
            {
                const int previous = winding;
                winding += crossing.winding;
                if (previous == 0 && winding != 0)
                {
                    spanStart = crossing.x;
                }
                else if (previous != 0 && winding == 0)
                {
                    if (settings.method == ScanConversion::AnalyticHorizontal)
                    {
                        AccumulateSpan(row, spanStart, crossing.x, sampleWeight);
                    }
                    else
                    {
                        AccumulateSampled(
                            row, spanStart, crossing.x, sampleWeight,
                            (std::max)(1u, settings.horizontalSamples));
                    }
                }
            }
        }

        std::byte* const scanline = bitmap.alpha.data() + static_cast<std::size_t>(y) * width;
        for (unsigned int x = 0; x < width; ++x)
        {
            const float coverage = std::clamp(row[x], 0.0f, 1.0f);
            scanline[x] = static_cast<std::byte>(coverage * 255.0f + 0.5f);
        }
    }
    return true;
}

}
