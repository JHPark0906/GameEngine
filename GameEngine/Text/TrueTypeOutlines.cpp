#include "pch.h"
#include "TrueTypeOutlines.h"

#include <cstddef>
#include <span>
#include <vector>

namespace GameEngine::Text
{

namespace
{
    [[nodiscard]] bool ReadUInt16(
        const std::span<const std::byte> bytes, const std::size_t offset, std::uint16_t& value)
    {
        if (offset + 2 > bytes.size())
        {
            return false;
        }
        value = static_cast<std::uint16_t>(
            (static_cast<unsigned int>(bytes[offset]) << 8) |
            static_cast<unsigned int>(bytes[offset + 1]));
        return true;
    }

    [[nodiscard]] bool ReadInt16(
        const std::span<const std::byte> bytes, const std::size_t offset, std::int16_t& value)
    {
        std::uint16_t raw = 0;
        if (!ReadUInt16(bytes, offset, raw))
        {
            return false;
        }
        value = static_cast<std::int16_t>(raw);
        return true;
    }

    [[nodiscard]] bool ReadUInt32(
        const std::span<const std::byte> bytes, const std::size_t offset, std::uint32_t& value)
    {
        if (offset + 4 > bytes.size())
        {
            return false;
        }
        value = (static_cast<std::uint32_t>(bytes[offset]) << 24) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
            static_cast<std::uint32_t>(bytes[offset + 3]);
        return true;
    }

    /// <summary>단순 글리프의 점 하나다. 곡선 위에 있는지 여부가 형태를 정한다.</summary>
    struct ContourPoint
    {
        float x = 0.0f;
        float y = 0.0f;
        bool onCurve = false;
    };

    /// <summary>2x2 변환과 이동이다. 합성 글리프의 부품이 이것으로 놓인다.</summary>
    struct ComponentTransform
    {
        float a = 1.0f;
        float b = 0.0f;
        float c = 0.0f;
        float d = 1.0f;
        float dx = 0.0f;
        float dy = 0.0f;

        [[nodiscard]] OutlinePoint Apply(const OutlinePoint point) const
        {
            return OutlinePoint{ a * point.x + c * point.y + dx, b * point.x + d * point.y + dy };
        }
    };

    constexpr std::uint8_t OnCurveFlag = 0x01;
    constexpr std::uint8_t XShortFlag = 0x02;
    constexpr std::uint8_t YShortFlag = 0x04;
    constexpr std::uint8_t RepeatFlag = 0x08;
    constexpr std::uint8_t XSameOrPositiveFlag = 0x10;
    constexpr std::uint8_t YSameOrPositiveFlag = 0x20;

    constexpr std::uint16_t ArgsAreWords = 0x0001;
    constexpr std::uint16_t ArgsAreXYValues = 0x0002;
    constexpr std::uint16_t HasScale = 0x0008;
    constexpr std::uint16_t MoreComponents = 0x0020;
    constexpr std::uint16_t HasXAndYScale = 0x0040;
    constexpr std::uint16_t HasTwoByTwo = 0x0080;

    /// <summary>F2Dot14 고정소수점이다. 합성 글리프의 배율이 이 형식이다.</summary>
    [[nodiscard]] float ToF2Dot14(const std::int16_t raw)
    {
        return static_cast<float>(raw) / 16384.0f;
    }

    [[nodiscard]] OutlinePoint Midpoint(const OutlinePoint left, const OutlinePoint right)
    {
        return OutlinePoint{ (left.x + right.x) * 0.5f, (left.y + right.y) * 0.5f };
    }

    /// <summary>
    /// 한 고리의 점들을 도막으로 바꾼다.
    ///
    /// TrueType은 곡선 위의 점과 밖의 점을 섞어 늘어놓고, <b>곡선 밖 점이 연달아 나오면 그
    /// 사이에 곡선 위의 점이 있는 것으로 친다</b> — 파일에 적히지 않은 중점이다. 이 암묵 규칙과
    /// 「고리가 곡선 밖 점에서 시작할 수 있다」가 glyf에서 가장 자주 틀리는 두 곳이라, 시작점을
    /// 먼저 정하고 나서 한 방향으로만 훑는다.
    /// </summary>
    void BuildContour(const std::vector<ContourPoint>& points, GlyphOutline& outline)
    {
        if (points.size() < 2)
        {
            return;
        }

        // 시작점을 정한다. 첫 점이 곡선 위면 그것이고, 아니면 마지막 점을 쓰되 그것마저 곡선
        // 밖이면 둘의 중점이 시작점이다 — 그 중점은 파일에 없는 점이다.
        std::size_t first = 0;
        OutlinePoint start{};
        if (points.front().onCurve)
        {
            start = OutlinePoint{ points.front().x, points.front().y };
            first = 1;
        }
        else if (points.back().onCurve)
        {
            start = OutlinePoint{ points.back().x, points.back().y };
            first = 0;
        }
        else
        {
            start = Midpoint(
                OutlinePoint{ points.front().x, points.front().y },
                OutlinePoint{ points.back().x, points.back().y });
            first = 0;
        }

        OutlineContour contour;
        contour.start = start;
        OutlinePoint previous = start;
        bool hasPendingControl = false;
        OutlinePoint pendingControl{};

        const std::size_t count = points.size();
        for (std::size_t step = 0; step < count; ++step)
        {
            const ContourPoint& point = points[(first + step) % count];
            const OutlinePoint here{ point.x, point.y };
            if (point.onCurve)
            {
                if (hasPendingControl)
                {
                    contour.segments.push_back(PromoteQuadratic(previous, pendingControl, here));
                    hasPendingControl = false;
                }
                else
                {
                    contour.segments.push_back(PromoteLine(previous, here));
                }
                previous = here;
                continue;
            }
            if (hasPendingControl)
            {
                // 곡선 밖 점이 둘 연달았다. 그 사이의 중점이 암묵적인 곡선 위의 점이다.
                const OutlinePoint implied = Midpoint(pendingControl, here);
                contour.segments.push_back(PromoteQuadratic(previous, pendingControl, implied));
                previous = implied;
            }
            pendingControl = here;
            hasPendingControl = true;
        }

        // 고리를 닫는다. 남은 제어점이 있으면 그것으로 시작점까지 굽고, 없으면 직선으로 닫는다.
        if (hasPendingControl)
        {
            contour.segments.push_back(PromoteQuadratic(previous, pendingControl, start));
        }
        else
        {
            contour.segments.push_back(PromoteLine(previous, start));
        }

        if (!contour.segments.empty())
        {
            outline.contours.push_back(std::move(contour));
        }
    }

    /// <summary>글리프 하나의 glyf 안 바이트다. 빈 span은 잉크 없는 글리프다.</summary>
    [[nodiscard]] bool FindGlyphBytes(
        const FontFace& face, const std::uint16_t glyphId, std::span<const std::byte>& glyphBytes)
    {
        const std::span<const std::byte> head = face.GetTable("head");
        std::int16_t locaFormat = 0;
        if (!ReadInt16(head, 50, locaFormat))
        {
            return false;
        }
        const std::span<const std::byte> loca = face.GetTable("loca");
        const std::span<const std::byte> glyf = face.GetTable("glyf");

        std::uint32_t start = 0;
        std::uint32_t end = 0;
        if (locaFormat == 0)
        {
            std::uint16_t shortStart = 0;
            std::uint16_t shortEnd = 0;
            if (!ReadUInt16(loca, static_cast<std::size_t>(glyphId) * 2, shortStart) ||
                !ReadUInt16(loca, static_cast<std::size_t>(glyphId) * 2 + 2, shortEnd))
            {
                return false;
            }
            // 짧은 형식은 오프셋을 2로 나눠 담는다.
            start = static_cast<std::uint32_t>(shortStart) * 2;
            end = static_cast<std::uint32_t>(shortEnd) * 2;
        }
        else if (
            !ReadUInt32(loca, static_cast<std::size_t>(glyphId) * 4, start) ||
            !ReadUInt32(loca, static_cast<std::size_t>(glyphId) * 4 + 4, end))
        {
            return false;
        }

        if (end < start || end > glyf.size())
        {
            return false;
        }
        // 시작과 끝이 같으면 그 글리프에는 윤곽선이 없다 — 공백이 그렇고, 실패가 아니다.
        glyphBytes = glyf.subspan(start, end - start);
        return true;
    }

    [[nodiscard]] bool AppendGlyph(
        const FontFace& face,
        std::uint16_t glyphId,
        const ComponentTransform& transform,
        unsigned int depth,
        GlyphOutline& outline);

    [[nodiscard]] bool AppendSimpleGlyph(
        const std::span<const std::byte> glyphBytes,
        const int contourCount,
        const ComponentTransform& transform,
        GlyphOutline& outline)
    {
        // 파일에 헤더가 있는 빈 글리프도 유효하다. loca 구간이 비는 경우와 같은 답이다.
        if (contourCount == 0)
        {
            return true;
        }
        std::vector<std::uint16_t> contourEnds(static_cast<std::size_t>(contourCount));
        std::size_t cursor = 10;
        for (int index = 0; index < contourCount; ++index)
        {
            if (!ReadUInt16(glyphBytes, cursor, contourEnds[static_cast<std::size_t>(index)]))
            {
                return false;
            }
            if (index > 0 && contourEnds[static_cast<std::size_t>(index)] <=
                contourEnds[static_cast<std::size_t>(index - 1)])
            {
                return false;
            }
            cursor += 2;
        }
        const std::size_t pointCount = static_cast<std::size_t>(contourEnds.back()) + 1;

        std::uint16_t instructionLength = 0;
        if (!ReadUInt16(glyphBytes, cursor, instructionLength))
        {
            return false;
        }
        // 힌팅 명령은 읽지 않고 건너뛴다. 이 엔진은 힌팅을 하지 않으므로 그 바이트는 길이만
        // 의미가 있다.
        cursor += 2 + instructionLength;
        if (cursor > glyphBytes.size())
        {
            return false;
        }

        std::vector<std::uint8_t> flags;
        flags.reserve(pointCount);
        while (flags.size() < pointCount)
        {
            if (cursor >= glyphBytes.size())
            {
                return false;
            }
            const auto flag = static_cast<std::uint8_t>(glyphBytes[cursor++]);
            flags.push_back(flag);
            if ((flag & RepeatFlag) == 0)
            {
                continue;
            }
            if (cursor >= glyphBytes.size())
            {
                return false;
            }
            // 반복 플래그 뒤의 바이트는 「같은 플래그를 몇 번 더」이다.
            const auto repeats = static_cast<std::uint8_t>(glyphBytes[cursor++]);
            if (repeats > pointCount - flags.size())
            {
                return false;
            }
            for (unsigned int repeat = 0; repeat < repeats; ++repeat)
            {
                flags.push_back(flag);
            }
        }
        if (flags.size() != pointCount)
        {
            return false;
        }

        // 좌표는 앞 점으로부터의 차이로 담긴다. 짧은 형식이면 한 바이트에 부호는 다른 플래그가
        // 들고, 긴 형식이면 두 바이트인데 그 플래그가 서면 「앞과 같다」는 뜻이라 아무것도 읽지
        // 않는다 — 같은 비트가 형식에 따라 뜻이 달라지는 자리다.
        const auto readCoordinates =
            [&](const std::uint8_t shortFlag, const std::uint8_t sameOrPositiveFlag,
                std::vector<float>& values)
        {
            float current = 0.0f;
            values.reserve(pointCount);
            for (const std::uint8_t flag : flags)
            {
                if ((flag & shortFlag) != 0)
                {
                    if (cursor >= glyphBytes.size())
                    {
                        return false;
                    }
                    const auto magnitude = static_cast<float>(
                        static_cast<std::uint8_t>(glyphBytes[cursor++]));
                    current += (flag & sameOrPositiveFlag) != 0 ? magnitude : -magnitude;
                }
                else if ((flag & sameOrPositiveFlag) == 0)
                {
                    std::int16_t delta = 0;
                    if (!ReadInt16(glyphBytes, cursor, delta))
                    {
                        return false;
                    }
                    cursor += 2;
                    current += static_cast<float>(delta);
                }
                values.push_back(current);
            }
            return true;
        };

        std::vector<float> xs;
        std::vector<float> ys;
        if (!readCoordinates(XShortFlag, XSameOrPositiveFlag, xs) ||
            !readCoordinates(YShortFlag, YSameOrPositiveFlag, ys))
        {
            return false;
        }

        std::size_t pointIndex = 0;
        for (const std::uint16_t contourEnd : contourEnds)
        {
            std::vector<ContourPoint> points;
            for (; pointIndex <= contourEnd && pointIndex < pointCount; ++pointIndex)
            {
                const OutlinePoint placed =
                    transform.Apply(OutlinePoint{ xs[pointIndex], ys[pointIndex] });
                points.push_back(ContourPoint{
                    placed.x, placed.y, (flags[pointIndex] & OnCurveFlag) != 0 });
            }
            BuildContour(points, outline);
        }
        return true;
    }

    [[nodiscard]] bool AppendCompositeGlyph(
        const FontFace& face,
        const std::span<const std::byte> glyphBytes,
        const ComponentTransform& transform,
        const unsigned int depth,
        GlyphOutline& outline)
    {
        std::size_t cursor = 10;
        for (;;)
        {
            std::uint16_t flags = 0;
            std::uint16_t componentGlyph = 0;
            if (!ReadUInt16(glyphBytes, cursor, flags) ||
                !ReadUInt16(glyphBytes, cursor + 2, componentGlyph))
            {
                return false;
            }
            cursor += 4;

            float dx = 0.0f;
            float dy = 0.0f;
            if ((flags & ArgsAreWords) != 0)
            {
                std::int16_t first = 0;
                std::int16_t second = 0;
                if (!ReadInt16(glyphBytes, cursor, first) ||
                    !ReadInt16(glyphBytes, cursor + 2, second))
                {
                    return false;
                }
                dx = static_cast<float>(first);
                dy = static_cast<float>(second);
                cursor += 4;
            }
            else
            {
                if (cursor + 2 > glyphBytes.size())
                {
                    return false;
                }
                dx = static_cast<float>(static_cast<std::int8_t>(glyphBytes[cursor]));
                dy = static_cast<float>(static_cast<std::int8_t>(glyphBytes[cursor + 1]));
                cursor += 2;
            }
            if ((flags & ArgsAreXYValues) == 0)
            {
                // 인자가 좌표가 아니라 「이 부품의 점 번호를 저 점 번호에 맞춰라」인 형식이다.
                // 동봉 폰트에 쓰이지 않으므로 옮기지 않는다 — 지어내는 것보다 안 그리는 편이 낫다.
                dx = 0.0f;
                dy = 0.0f;
            }

            ComponentTransform component;
            component.dx = dx;
            component.dy = dy;
            if ((flags & HasScale) != 0)
            {
                std::int16_t scale = 0;
                if (!ReadInt16(glyphBytes, cursor, scale))
                {
                    return false;
                }
                cursor += 2;
                component.a = ToF2Dot14(scale);
                component.d = component.a;
            }
            else if ((flags & HasXAndYScale) != 0)
            {
                std::int16_t scaleX = 0;
                std::int16_t scaleY = 0;
                if (!ReadInt16(glyphBytes, cursor, scaleX) ||
                    !ReadInt16(glyphBytes, cursor + 2, scaleY))
                {
                    return false;
                }
                cursor += 4;
                component.a = ToF2Dot14(scaleX);
                component.d = ToF2Dot14(scaleY);
            }
            else if ((flags & HasTwoByTwo) != 0)
            {
                std::int16_t values[4]{};
                for (std::size_t index = 0; index < 4; ++index)
                {
                    if (!ReadInt16(glyphBytes, cursor + index * 2, values[index]))
                    {
                        return false;
                    }
                }
                cursor += 8;
                component.a = ToF2Dot14(values[0]);
                component.b = ToF2Dot14(values[1]);
                component.c = ToF2Dot14(values[2]);
                component.d = ToF2Dot14(values[3]);
            }

            // 부품의 변환을 바깥 변환과 합친다. 부품이 또 합성일 수 있으므로 곱해 내려간다.
            ComponentTransform combined;
            combined.a = component.a * transform.a + component.b * transform.c;
            combined.b = component.a * transform.b + component.b * transform.d;
            combined.c = component.c * transform.a + component.d * transform.c;
            combined.d = component.c * transform.b + component.d * transform.d;
            const OutlinePoint offset = transform.Apply(OutlinePoint{ component.dx, component.dy });
            combined.dx = offset.x;
            combined.dy = offset.y;

            if (!AppendGlyph(face, componentGlyph, combined, depth + 1, outline))
            {
                return false;
            }
            if ((flags & MoreComponents) == 0)
            {
                return true;
            }
        }
    }

    bool AppendGlyph(
        const FontFace& face,
        const std::uint16_t glyphId,
        const ComponentTransform& transform,
        const unsigned int depth,
        GlyphOutline& outline)
    {
        // 합성 글리프는 다른 글리프를 부르고 그것이 또 합성일 수 있다. 파일이 자기를 부르는
        // 고리를 만들면 여기서 끝이 없으므로 깊이로 끊는다 — 실제 폰트는 두세 겹을 넘지 않는다.
        constexpr unsigned int MaximumDepth = 8;
        if (depth > MaximumDepth || glyphId >= face.GetGlyphCount())
        {
            return false;
        }

        std::span<const std::byte> glyphBytes;
        if (!FindGlyphBytes(face, glyphId, glyphBytes))
        {
            return false;
        }
        if (glyphBytes.empty())
        {
            return true;
        }

        std::int16_t contourCount = 0;
        if (!ReadInt16(glyphBytes, 0, contourCount))
        {
            return false;
        }
        if (contourCount >= 0)
        {
            return AppendSimpleGlyph(glyphBytes, contourCount, transform, outline);
        }
        return AppendCompositeGlyph(face, glyphBytes, transform, depth, outline);
    }
}

bool GetTrueTypeGlyphOutline(
    const FontFace& face, const std::uint16_t glyphId, GlyphOutline& outline)
{
    outline.contours.clear();
    if (!face.IsValid() || face.GetOutlineFormat() != FontFace::OutlineFormat::TrueType)
    {
        return false;
    }
    return AppendGlyph(face, glyphId, ComponentTransform{}, 0, outline);
}

}
