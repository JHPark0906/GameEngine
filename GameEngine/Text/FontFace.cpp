#include "pch.h"
#include "FontFace.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace GameEngine::Text
{

namespace
{
    /// <summary>
    /// 경계를 확인하며 큰 끝 정수를 읽는다. 폰트 파일의 수는 전부 큰 끝이다.
    ///
    /// 읽기마다 범위를 확인하는 것이 이 파일의 규칙이다. 손상된 폰트가 이 코드를 배열 밖으로
    /// 데려가면 그것은 글자가 안 나오는 일이 아니라 프로세스가 죽는 일이 되고, 폰트는 콘텐츠라
    /// 사람이 갈아 끼울 수 있다.
    /// </summary>
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

    /// <summary>sfnt 버전이 말하는 윤곽선 형식이다. 둘 다 아니면 우리가 읽을 파일이 아니다.</summary>
    constexpr std::uint32_t TrueTypeVersion = 0x00010000u;
    constexpr std::uint32_t OpenTypeVersion = 0x4F54544Fu;  // 'OTTO'
}

bool FontFace::Parse(const std::span<const std::byte> fontBytes)
{
    mValid = false;
    mTables.clear();
    mCharacterMap = {};
    mBytes.assign(fontBytes.begin(), fontBytes.end());
    const std::span<const std::byte> bytes{ mBytes };

    std::uint32_t version = 0;
    std::uint16_t tableCount = 0;
    if (!ReadUInt32(bytes, 0, version) || !ReadUInt16(bytes, 4, tableCount))
    {
        return false;
    }
    if (version == TrueTypeVersion)
    {
        mOutlineFormat = OutlineFormat::TrueType;
    }
    else if (version == OpenTypeVersion)
    {
        mOutlineFormat = OutlineFormat::CompactFontFormat;
    }
    else
    {
        // 컬렉션('ttcf')과 그 밖의 것은 아직 읽지 않는다. 동봉 폰트에 하나도 없다.
        return false;
    }

    for (unsigned int index = 0; index < tableCount; ++index)
    {
        const std::size_t record = 12 + static_cast<std::size_t>(index) * 16;
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
        if (record + 16 > bytes.size() || !ReadUInt32(bytes, record + 8, offset) ||
            !ReadUInt32(bytes, record + 12, length))
        {
            return false;
        }
        // 파일 밖을 가리키는 테이블은 그 테이블만 버리지 않고 파일을 버린다. 하나가 거짓이면
        // 나머지 오프셋도 믿을 이유가 없다.
        if (static_cast<std::size_t>(offset) + length > bytes.size())
        {
            return false;
        }
        std::string tag(4, '\0');
        for (std::size_t character = 0; character < 4; ++character)
        {
            tag[character] = static_cast<char>(bytes[record + character]);
        }
        mTables.emplace(std::move(tag), bytes.subspan(offset, length));
    }

    if (!ReadHead() || !ReadMaxp() || !ReadHorizontalMetrics() || !ReadCharacterMap())
    {
        return false;
    }
    ReadVerticalMetrics();

    // 선언한 형식의 윤곽선 테이블이 실제로 있어야 이 face로 글자를 그릴 수 있다.
    const bool hasOutlines = mOutlineFormat == OutlineFormat::TrueType
        ? (!GetTable("glyf").empty() && !GetTable("loca").empty())
        : !GetTable("CFF ").empty();
    if (!hasOutlines)
    {
        return false;
    }

    mValid = true;
    return true;
}

std::span<const std::byte> FontFace::GetTable(const std::string_view tag) const
{
    const auto found = mTables.find(std::string(tag));
    return found == mTables.end() ? std::span<const std::byte>{} : found->second;
}

bool FontFace::ReadHead()
{
    const std::span<const std::byte> head = GetTable("head");
    std::uint16_t unitsPerEm = 0;
    if (!ReadUInt16(head, 18, unitsPerEm) || unitsPerEm == 0)
    {
        return false;
    }
    mUnitsPerEm = unitsPerEm;
    return true;
}

bool FontFace::ReadMaxp()
{
    std::uint16_t glyphCount = 0;
    if (!ReadUInt16(GetTable("maxp"), 4, glyphCount))
    {
        return false;
    }
    mGlyphCount = glyphCount;
    return glyphCount > 0;
}

bool FontFace::ReadHorizontalMetrics()
{
    std::uint16_t metricCount = 0;
    if (!ReadUInt16(GetTable("hhea"), 34, metricCount) || metricCount == 0)
    {
        return false;
    }
    mHorizontalMetricCount = metricCount;
    return !GetTable("hmtx").empty();
}

void FontFace::ReadVerticalMetrics()
{
    const std::span<const std::byte> hhea = GetTable("hhea");
    std::int16_t value = 0;
    if (ReadInt16(hhea, 4, value)) { mVerticalMetrics.hheaAscender = value; }
    if (ReadInt16(hhea, 6, value)) { mVerticalMetrics.hheaDescender = value; }
    if (ReadInt16(hhea, 8, value)) { mVerticalMetrics.hheaLineGap = value; }

    const std::span<const std::byte> os2 = GetTable("OS/2");
    if (os2.empty())
    {
        return;
    }
    // sTypoAscender는 버전과 무관하게 68바이트째다. usWinAscent는 그 뒤 74·76이다.
    std::uint16_t unsignedValue = 0;
    if (ReadInt16(os2, 68, value)) { mVerticalMetrics.typoAscender = value; }
    if (ReadInt16(os2, 70, value)) { mVerticalMetrics.typoDescender = value; }
    if (ReadInt16(os2, 72, value)) { mVerticalMetrics.typoLineGap = value; }
    if (ReadUInt16(os2, 74, unsignedValue)) { mVerticalMetrics.winAscent = unsignedValue; }
    if (ReadUInt16(os2, 76, unsignedValue)) { mVerticalMetrics.winDescent = unsignedValue; }
    mVerticalMetrics.hasOs2 = true;
}

bool FontFace::ReadCharacterMap()
{
    const std::span<const std::byte> cmap = GetTable("cmap");
    std::uint16_t subtableCount = 0;
    if (!ReadUInt16(cmap, 2, subtableCount))
    {
        return false;
    }

    // 윈도우 유니코드 부분표(platform 3)의 format 4를 고른다. 동봉 폰트 열넷이 전부 이것을
    // 갖고 있고, format 4는 기본 다국어 평면 전체 — 한글 완성형과 라틴 — 를 덮는다. 평면 밖
    // 글자를 쓰게 되면 그때 format 12를 더한다.
    for (unsigned int index = 0; index < subtableCount; ++index)
    {
        const std::size_t record = 4 + static_cast<std::size_t>(index) * 8;
        std::uint16_t platform = 0;
        std::uint32_t subtableOffset = 0;
        if (!ReadUInt16(cmap, record, platform) || !ReadUInt32(cmap, record + 4, subtableOffset))
        {
            return false;
        }
        if (platform != 3 || subtableOffset >= cmap.size())
        {
            continue;
        }
        const std::span<const std::byte> subtable = cmap.subspan(subtableOffset);
        std::uint16_t format = 0;
        if (!ReadUInt16(subtable, 0, format) || format != 4)
        {
            continue;
        }
        mCharacterMap = subtable;
        return true;
    }
    return false;
}

std::uint16_t FontFace::GetGlyphIndex(const char32_t codePoint) const
{
    // format 4는 기본 다국어 평면만 담는다. 그 밖은 이 폰트가 답할 수 없다.
    if (mCharacterMap.empty() || codePoint > 0xFFFF)
    {
        return 0;
    }
    const auto character = static_cast<std::uint16_t>(codePoint);

    std::uint16_t segCountX2 = 0;
    if (!ReadUInt16(mCharacterMap, 6, segCountX2) || segCountX2 == 0)
    {
        return 0;
    }
    const std::size_t segCount = segCountX2 / 2u;
    const std::size_t endCodes = 14;
    const std::size_t startCodes = endCodes + segCountX2 + 2;
    const std::size_t deltas = startCodes + segCountX2;
    const std::size_t rangeOffsets = deltas + segCountX2;

    for (std::size_t segment = 0; segment < segCount; ++segment)
    {
        std::uint16_t endCode = 0;
        if (!ReadUInt16(mCharacterMap, endCodes + segment * 2, endCode) || endCode < character)
        {
            continue;
        }
        std::uint16_t startCode = 0;
        if (!ReadUInt16(mCharacterMap, startCodes + segment * 2, startCode) ||
            startCode > character)
        {
            return 0;
        }

        std::uint16_t idDelta = 0;
        std::uint16_t idRangeOffset = 0;
        if (!ReadUInt16(mCharacterMap, deltas + segment * 2, idDelta) ||
            !ReadUInt16(mCharacterMap, rangeOffsets + segment * 2, idRangeOffset))
        {
            return 0;
        }
        if (idRangeOffset == 0)
        {
            return static_cast<std::uint16_t>((character + idDelta) & 0xFFFF);
        }

        // idRangeOffset은 <b>자기 자신이 놓인 자리</b>에서부터 센 바이트 거리다. 이 상대성이
        // format 4에서 가장 자주 틀리는 곳이라, 슬롯의 절대 위치를 먼저 적고 거기서 더한다.
        const std::size_t slot = rangeOffsets + segment * 2;
        const std::size_t target =
            slot + idRangeOffset + static_cast<std::size_t>(character - startCode) * 2;
        std::uint16_t glyphId = 0;
        if (!ReadUInt16(mCharacterMap, target, glyphId) || glyphId == 0)
        {
            return 0;
        }
        return static_cast<std::uint16_t>((glyphId + idDelta) & 0xFFFF);
    }
    return 0;
}

std::uint16_t FontFace::GetAdvanceWidth(const std::uint16_t glyphId) const
{
    const std::span<const std::byte> hmtx = GetTable("hmtx");
    // 마지막 진행폭 뒤의 글리프들은 그 값을 나눠 쓴다. 고정폭 폰트가 표를 한 줄로 줄이는 방법이다.
    const unsigned int index =
        glyphId < mHorizontalMetricCount ? glyphId : mHorizontalMetricCount - 1;
    std::uint16_t advance = 0;
    if (!ReadUInt16(hmtx, static_cast<std::size_t>(index) * 4, advance))
    {
        return 0;
    }
    return advance;
}

}
