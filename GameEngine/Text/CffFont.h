#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Type2Charstring.h"

namespace GameEngine::Text
{

/// <summary>
/// 글리프가 어느 폰트 DICT에 속하는지를 <c>FDSelect</c>에서 읽는다.
///
/// 자유 함수인 것은 시험 때문이다. 동봉 폰트가 형식 하나만 쓰면 다른 형식은 제품 코드에서 영영
/// 안 밟히는 갈래가 되는데, 그런 갈래는 시험만 밟거나 아무도 안 밟거나 둘 중 하나다. 손으로
/// 지은 표를 이 함수에 직접 먹여 두 형식을 다 밟는 편이 낫다.
/// </summary>
/// <param name="fdSelect">FDSelect 테이블의 바이트다.</param>
/// <param name="glyphId">찾을 글리프 번호다.</param>
/// <param name="glyphCount">폰트의 글리프 수다. 형식 0의 길이를 확인하는 데 쓴다.</param>
/// <param name="fontDictIndex">폰트 DICT 번호를 받는다.</param>
/// <returns>찾았으면 true다. 형식을 모르거나 표가 짧으면 false다.</returns>
[[nodiscard]] bool ReadFontDictIndex(
    std::span<const std::byte> fdSelect,
    std::uint16_t glyphId,
    unsigned int glyphCount,
    unsigned int& fontDictIndex);

/// <summary>
/// OpenType 폰트 안의 <c>CFF </c> 테이블이다. 글리프의 윤곽선이 Type 2 charstring으로 들어 있다.
///
/// <b>CID 키 방식</b>을 함께 읽는다. 동봉된 한글 OTF 열이 전부 그 방식이고, 그때는 Private
/// DICT가 폰트에 하나가 아니라 <b>글리프마다</b> 있다 — <c>FDSelect</c>로 번호를 찾고
/// <c>FDArray</c>에서 그 DICT를 얻어야 지역 subr 목록이 정해진다. 이것을 모르고 Top DICT의
/// Private을 쓰면 글자가 죽지 않고 미묘하게 어긋난다.
/// </summary>
class CffFont final
{
public:
    /// <summary>CFF 테이블의 바이트를 읽는다. 사본을 쥐지 않으므로 바이트가 더 오래 살아야 한다.</summary>
    [[nodiscard]] bool Parse(std::span<const std::byte> cffTable);

    [[nodiscard]] bool IsValid() const { return mValid; }
    [[nodiscard]] bool IsCidKeyed() const { return mIsCidKeyed; }
    [[nodiscard]] unsigned int GetGlyphCount() const
    {
        return static_cast<unsigned int>(mCharStrings.size());
    }

    /// <summary>이 폰트가 쓰는 FDSelect의 형식이다. CID가 아니면 0이 아닌 값이 아니다.</summary>
    [[nodiscard]] unsigned int GetFontDictSelectFormat() const { return mFdSelectFormat; }
    [[nodiscard]] unsigned int GetFontDictCount() const
    {
        return static_cast<unsigned int>(mFontDicts.size());
    }

    /// <summary>글리프 하나의 윤곽선과 진행폭을 낸다.</summary>
    [[nodiscard]] bool GetGlyphOutline(std::uint16_t glyphId, CharstringResult& result) const;

private:
    /// <summary>한 Private DICT가 정하는 것들이다. CID 폰트는 이것을 여럿 갖는다.</summary>
    struct PrivateDict
    {
        std::vector<std::span<const std::byte>> localSubroutines;
        float defaultWidthX = 0.0f;
        float nominalWidthX = 0.0f;
    };

    [[nodiscard]] bool ReadPrivateDict(std::size_t offset, std::size_t size, PrivateDict& into);

    std::span<const std::byte> mBytes;
    std::vector<std::span<const std::byte>> mCharStrings;
    std::vector<std::span<const std::byte>> mGlobalSubroutines;
    std::vector<PrivateDict> mFontDicts;
    std::span<const std::byte> mFdSelect;
    unsigned int mFdSelectFormat = 0;
    bool mIsCidKeyed = false;
    bool mValid = false;
};

}
