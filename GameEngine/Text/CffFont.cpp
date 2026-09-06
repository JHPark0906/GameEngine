#include "pch.h"
#include "CffFont.h"

#include <unordered_map>

namespace GameEngine::Text
{

namespace
{
    [[nodiscard]] bool ReadUInt8(
        const std::span<const std::byte> bytes, const std::size_t offset, unsigned int& value)
    {
        if (offset >= bytes.size())
        {
            return false;
        }
        value = static_cast<unsigned int>(static_cast<unsigned char>(bytes[offset]));
        return true;
    }

    [[nodiscard]] bool ReadUInt16(
        const std::span<const std::byte> bytes, const std::size_t offset, unsigned int& value)
    {
        if (offset + 2 > bytes.size())
        {
            return false;
        }
        value = (static_cast<unsigned int>(static_cast<unsigned char>(bytes[offset])) << 8) |
            static_cast<unsigned int>(static_cast<unsigned char>(bytes[offset + 1]));
        return true;
    }

    /// <summary>
    /// CFF의 INDEX 하나다. 길이가 다른 조각들을 오프셋 표로 늘어놓은 구조이며, CFF는 거의 모든
    /// 것을 이 형태로 담는다.
    /// </summary>
    struct Index
    {
        std::vector<std::span<const std::byte>> items;
        /// <summary>이 INDEX 바로 뒤의 자리다. 다음 INDEX가 거기서 시작한다.</summary>
        std::size_t end = 0;
    };

    [[nodiscard]] bool ReadIndex(
        const std::span<const std::byte> bytes, const std::size_t position, Index& index)
    {
        unsigned int count = 0;
        if (!ReadUInt16(bytes, position, count))
        {
            return false;
        }
        if (count == 0)
        {
            // 빈 INDEX는 개수 두 바이트가 전부다. 오프셋 크기조차 없다.
            index.end = position + 2;
            return true;
        }

        unsigned int offsetSize = 0;
        if (!ReadUInt8(bytes, position + 2, offsetSize) || offsetSize == 0 || offsetSize > 4)
        {
            return false;
        }
        const std::size_t offsets = position + 3;
        const auto readOffset = [&](const std::size_t which, std::size_t& value)
        {
            const std::size_t at = offsets + which * offsetSize;
            if (at + offsetSize > bytes.size())
            {
                return false;
            }
            value = 0;
            for (std::size_t byte = 0; byte < offsetSize; ++byte)
            {
                value = (value << 8) |
                    static_cast<std::size_t>(static_cast<unsigned char>(bytes[at + byte]));
            }
            return true;
        };

        // 오프셋은 1부터 세므로 데이터의 시작을 한 칸 당겨 둔다.
        const std::size_t data = offsets + (static_cast<std::size_t>(count) + 1) * offsetSize - 1;
        std::size_t previous = 0;
        if (!readOffset(0, previous))
        {
            return false;
        }
        index.items.reserve(count);
        for (unsigned int item = 0; item < count; ++item)
        {
            std::size_t next = 0;
            if (!readOffset(item + 1, next) || next < previous || data + next > bytes.size())
            {
                return false;
            }
            index.items.push_back(bytes.subspan(data + previous, next - previous));
            previous = next;
        }
        index.end = data + previous;
        return true;
    }

    /// <summary>DICT 하나를 연산자 → 피연산자 목록으로 읽는다.</summary>
    using Dictionary = std::unordered_map<unsigned int, std::vector<double>>;

    /// <summary>두 바이트 연산자는 12를 앞에 붙인 것이라, 1200을 더해 한 열쇠로 만든다.</summary>
    [[nodiscard]] constexpr unsigned int EscapedOperator(const unsigned int op)
    {
        return 1200 + op;
    }

    [[nodiscard]] bool ReadDictionary(const std::span<const std::byte> bytes, Dictionary& into)
    {
        std::vector<double> operands;
        std::size_t cursor = 0;
        while (cursor < bytes.size())
        {
            const auto byte = static_cast<unsigned char>(bytes[cursor]);
            if (byte <= 21)
            {
                unsigned int key = byte;
                ++cursor;
                if (byte == 12)
                {
                    unsigned int escape = 0;
                    if (!ReadUInt8(bytes, cursor, escape))
                    {
                        return false;
                    }
                    key = EscapedOperator(escape);
                    ++cursor;
                }
                into[key] = operands;
                operands.clear();
                continue;
            }
            if (byte == 28)
            {
                unsigned int raw = 0;
                if (!ReadUInt16(bytes, cursor + 1, raw))
                {
                    return false;
                }
                operands.push_back(static_cast<double>(static_cast<std::int16_t>(raw)));
                cursor += 3;
                continue;
            }
            if (byte == 29)
            {
                if (cursor + 5 > bytes.size())
                {
                    return false;
                }
                std::int32_t value = 0;
                for (std::size_t index = 1; index <= 4; ++index)
                {
                    value = (value << 8) |
                        static_cast<std::int32_t>(static_cast<unsigned char>(bytes[cursor + index]));
                }
                operands.push_back(static_cast<double>(value));
                cursor += 5;
                continue;
            }
            if (byte == 30)
            {
                // 실수는 반바이트마다 숫자·기호를 담고 0xF로 끝난다. 값 자체는 이 파서가 쓰는
                // 곳이 없어(폰트 행렬은 읽지 않는다) 자리만 정확히 건너뛴다.
                ++cursor;
                bool ended = false;
                while (cursor < bytes.size() && !ended)
                {
                    const auto pair = static_cast<unsigned char>(bytes[cursor]);
                    ++cursor;
                    ended = (pair & 0x0F) == 0x0F || ((pair >> 4) & 0x0F) == 0x0F;
                }
                operands.push_back(0.0);
                continue;
            }
            if (byte >= 32 && byte <= 246)
            {
                operands.push_back(static_cast<double>(static_cast<int>(byte) - 139));
                ++cursor;
                continue;
            }
            if (byte >= 247 && byte <= 250)
            {
                unsigned int low = 0;
                if (!ReadUInt8(bytes, cursor + 1, low))
                {
                    return false;
                }
                operands.push_back(static_cast<double>((byte - 247) * 256 + low + 108));
                cursor += 2;
                continue;
            }
            if (byte >= 251 && byte <= 254)
            {
                unsigned int low = 0;
                if (!ReadUInt8(bytes, cursor + 1, low))
                {
                    return false;
                }
                operands.push_back(-static_cast<double>((byte - 251) * 256 + low + 108));
                cursor += 2;
                continue;
            }
            return false;
        }
        return true;
    }

    [[nodiscard]] double DictValue(
        const Dictionary& dictionary, const unsigned int key, const std::size_t which,
        const double fallback)
    {
        const auto found = dictionary.find(key);
        if (found == dictionary.end() || which >= found->second.size())
        {
            return fallback;
        }
        return found->second[which];
    }

    constexpr unsigned int OperatorCharStrings = 17;
    constexpr unsigned int OperatorPrivate = 18;
    constexpr unsigned int OperatorSubrs = 19;
    constexpr unsigned int OperatorDefaultWidthX = 20;
    constexpr unsigned int OperatorNominalWidthX = 21;
}

bool ReadFontDictIndex(
    const std::span<const std::byte> fdSelect,
    const std::uint16_t glyphId,
    const unsigned int glyphCount,
    unsigned int& fontDictIndex)
{
    unsigned int format = 0;
    if (!ReadUInt8(fdSelect, 0, format))
    {
        return false;
    }

    if (format == 0)
    {
        // 형식 0: 글리프마다 한 바이트가 늘어서 있다. 단순하지만 글리프가 많으면 커진다.
        if (glyphId >= glyphCount)
        {
            return false;
        }
        return ReadUInt8(fdSelect, 1 + static_cast<std::size_t>(glyphId), fontDictIndex);
    }
    if (format == 3)
    {
        // 형식 3: 구간 목록이다. 각 구간이 「이 글리프부터」와 DICT 번호를 담고, 마지막에
        // 끝나는 글리프 번호가 파수꾼으로 붙는다. 한글 폰트처럼 글리프가 만 개를 넘으면
        // 이쪽이 훨씬 작다.
        unsigned int ranges = 0;
        if (!ReadUInt16(fdSelect, 1, ranges) || ranges == 0)
        {
            return false;
        }
        unsigned int sentinel = 0;
        if (!ReadUInt16(fdSelect, 3 + static_cast<std::size_t>(ranges) * 3, sentinel) ||
            glyphId >= sentinel)
        {
            return false;
        }
        for (unsigned int range = 0; range < ranges; ++range)
        {
            const std::size_t entry = 3 + static_cast<std::size_t>(range) * 3;
            unsigned int first = 0;
            unsigned int next = 0;
            if (!ReadUInt16(fdSelect, entry, first) ||
                !ReadUInt16(fdSelect, entry + 3, next))
            {
                return false;
            }
            if (glyphId >= first && glyphId < next)
            {
                return ReadUInt8(fdSelect, entry + 2, fontDictIndex);
            }
        }
        return false;
    }
    return false;
}

bool CffFont::ReadPrivateDict(
    const std::size_t offset, const std::size_t size, PrivateDict& into)
{
    if (offset + size > mBytes.size())
    {
        return false;
    }
    const std::span<const std::byte> bytes = mBytes.subspan(offset, size);
    Dictionary dictionary;
    if (!ReadDictionary(bytes, dictionary))
    {
        return false;
    }
    into.defaultWidthX =
        static_cast<float>(DictValue(dictionary, OperatorDefaultWidthX, 0, 0.0));
    into.nominalWidthX =
        static_cast<float>(DictValue(dictionary, OperatorNominalWidthX, 0, 0.0));

    const auto subrs = dictionary.find(OperatorSubrs);
    if (subrs != dictionary.end() && !subrs->second.empty())
    {
        // 지역 subr의 오프셋은 <b>Private DICT의 시작</b>에서부터 센다. CharStrings처럼 파일
        // 시작에서 세는 것이 아니라서, 이 한 자리를 헷갈리면 엉뚱한 바이트를 subr로 읽는다.
        Index index;
        if (!ReadIndex(mBytes, offset + static_cast<std::size_t>(subrs->second[0]), index))
        {
            return false;
        }
        into.localSubroutines = std::move(index.items);
    }
    return true;
}

bool CffFont::Parse(const std::span<const std::byte> cffTable)
{
    mValid = false;
    mBytes = cffTable;
    mCharStrings.clear();
    mGlobalSubroutines.clear();
    mFontDicts.clear();
    mFdSelect = {};
    mFdSelectFormat = 0;
    mIsCidKeyed = false;

    unsigned int headerSize = 0;
    if (!ReadUInt8(mBytes, 2, headerSize) || headerSize < 4)
    {
        return false;
    }

    Index names;
    Index topDicts;
    Index strings;
    Index globalSubrs;
    if (!ReadIndex(mBytes, headerSize, names) || !ReadIndex(mBytes, names.end, topDicts) ||
        !ReadIndex(mBytes, topDicts.end, strings) || !ReadIndex(mBytes, strings.end, globalSubrs))
    {
        return false;
    }
    mGlobalSubroutines = std::move(globalSubrs.items);
    if (topDicts.items.empty())
    {
        return false;
    }

    Dictionary top;
    if (!ReadDictionary(topDicts.items.front(), top))
    {
        return false;
    }

    const auto charStrings = top.find(OperatorCharStrings);
    if (charStrings == top.end() || charStrings->second.empty())
    {
        return false;
    }
    Index charStringIndex;
    if (!ReadIndex(mBytes, static_cast<std::size_t>(charStrings->second[0]), charStringIndex))
    {
        return false;
    }
    mCharStrings = std::move(charStringIndex.items);
    if (mCharStrings.empty())
    {
        return false;
    }

    // ROS가 있으면 CID 키 방식이다. 그때는 Private DICT가 폰트에 하나가 아니라 FDArray에
    // 여럿 있고, 어느 것을 쓸지는 글리프마다 FDSelect가 정한다.
    mIsCidKeyed = top.find(EscapedOperator(30)) != top.end();
    if (mIsCidKeyed)
    {
        const auto fdArray = top.find(EscapedOperator(36));
        const auto fdSelect = top.find(EscapedOperator(37));
        if (fdArray == top.end() || fdArray->second.empty() || fdSelect == top.end() ||
            fdSelect->second.empty())
        {
            return false;
        }
        Index fontDictIndex;
        if (!ReadIndex(mBytes, static_cast<std::size_t>(fdArray->second[0]), fontDictIndex))
        {
            return false;
        }
        for (const std::span<const std::byte> fontDictBytes : fontDictIndex.items)
        {
            Dictionary fontDict;
            if (!ReadDictionary(fontDictBytes, fontDict))
            {
                return false;
            }
            const auto privateEntry = fontDict.find(OperatorPrivate);
            PrivateDict dict;
            if (privateEntry != fontDict.end() && privateEntry->second.size() >= 2 &&
                !ReadPrivateDict(
                    static_cast<std::size_t>(privateEntry->second[1]),
                    static_cast<std::size_t>(privateEntry->second[0]), dict))
            {
                return false;
            }
            mFontDicts.push_back(std::move(dict));
        }
        const auto selectOffset = static_cast<std::size_t>(fdSelect->second[0]);
        if (selectOffset >= mBytes.size())
        {
            return false;
        }
        mFdSelect = mBytes.subspan(selectOffset);
        if (!ReadUInt8(mFdSelect, 0, mFdSelectFormat))
        {
            return false;
        }
    }
    else
    {
        const auto privateEntry = top.find(OperatorPrivate);
        PrivateDict dict;
        if (privateEntry != top.end() && privateEntry->second.size() >= 2 &&
            !ReadPrivateDict(
                static_cast<std::size_t>(privateEntry->second[1]),
                static_cast<std::size_t>(privateEntry->second[0]), dict))
        {
            return false;
        }
        mFontDicts.push_back(std::move(dict));
    }

    mValid = !mFontDicts.empty();
    return mValid;
}

bool CffFont::GetGlyphOutline(const std::uint16_t glyphId, CharstringResult& result) const
{
    result = CharstringResult{};
    if (!mValid || glyphId >= mCharStrings.size())
    {
        return false;
    }

    // CID 폰트에서는 이 글리프가 쓸 Private DICT를 먼저 정해야 한다. 그것이 지역 subr 목록과
    // 폭 기준값을 함께 정하므로, 여기를 틀리면 해석이 조용히 어긋난다.
    std::size_t fontDict = 0;
    if (mIsCidKeyed)
    {
        unsigned int selected = 0;
        if (!ReadFontDictIndex(mFdSelect, glyphId, GetGlyphCount(), selected) ||
            selected >= mFontDicts.size())
        {
            return false;
        }
        fontDict = selected;
    }

    const PrivateDict& dict = mFontDicts[fontDict];
    CharstringContext context;
    context.globalSubroutines = mGlobalSubroutines;
    context.localSubroutines = dict.localSubroutines;
    context.defaultWidthX = dict.defaultWidthX;
    context.nominalWidthX = dict.nominalWidthX;
    return RunType2Charstring(mCharStrings[glyphId], context, result);
}

}
