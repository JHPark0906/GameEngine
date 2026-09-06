#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace GameEngine::Text
{

/// <summary>
/// 폰트 파일 하나를 읽어, 글자를 글리프로 바꾸고 그 글리프의 진행폭과 세로 지표를 답한다.
///
/// 이것은 <b>윤곽선을 다루지 않는다.</b> 윤곽선은 TrueType이면 glyf, OpenType이면 CFF에서
/// 오고 둘의 생김새가 전혀 다르므로, 그 둘을 각자의 파서에 두고 여기서는 <b>두 형식이 공유하는
/// 것</b>만 읽는다 — 어느 테이블이 어디 있는지, 글자와 글리프의 대응, 진행폭, 줄 높이.
///
/// 폰트 파일은 바깥에서 온 바이트다. 프로젝트 콘텐츠일 뿐이라 해도 손상된 파일이 이 코드를
/// 배열 밖으로 데려가서는 안 되므로, 모든 읽기는 경계를 확인하고 어긋나면 등록을 거절한다 —
/// 잘못된 폰트는 글자가 안 나오는 일이지 프로세스가 죽는 일이 아니다.
/// </summary>
class FontFace final
{
public:
    /// <summary>이 face가 담는 윤곽선의 형식이다. 어느 파서가 글리프를 그릴지 정한다.</summary>
    enum class OutlineFormat : unsigned char
    {
        /// <summary>glyf/loca. 2차 베지에다.</summary>
        TrueType,
        /// <summary>CFF. Type 2 charstring이고 3차 베지에다.</summary>
        CompactFontFormat,
    };

    /// <summary>
    /// 폰트 한 벌의 세로 지표다. 폰트 단위이며 크기를 곱하기 전의 값이다.
    ///
    /// hhea, OS/2 typo, OS/2 win 지표를 모두 읽는다. 줄 높이와 베이스라인의 선택은 배치하는
    /// 쪽의 몫이다. 세 규약의 총 줄 높이와 어센더는 폰트에 따라 다르므로 같은 값으로 가정하지 않는다.
    /// </summary>
    struct VerticalMetrics
    {
        int hheaAscender = 0;
        int hheaDescender = 0;
        int hheaLineGap = 0;
        int typoAscender = 0;
        int typoDescender = 0;
        int typoLineGap = 0;
        int winAscent = 0;
        int winDescent = 0;
        /// <summary>OS/2 테이블이 있었는지. 없으면 typo·win 값은 의미가 없다.</summary>
        bool hasOs2 = false;
    };

    FontFace() = default;

    /// <summary>
    /// 폰트 파일의 바이트를 읽는다. 사본을 쥐므로 호출자의 버퍼는 이 뒤에 사라져도 된다.
    /// </summary>
    /// <param name="fontBytes">폰트 파일 전체의 바이트다.</param>
    /// <returns>읽어서 쓸 수 있으면 true다. 실패하면 이 face는 비어 있는 채로 남는다.</returns>
    [[nodiscard]] bool Parse(std::span<const std::byte> fontBytes);

    [[nodiscard]] bool IsValid() const { return mValid; }
    [[nodiscard]] OutlineFormat GetOutlineFormat() const { return mOutlineFormat; }

    /// <summary>폰트 좌표계의 한 em이 몇 단위인가. 크기 배율의 분모다.</summary>
    [[nodiscard]] unsigned int GetUnitsPerEm() const { return mUnitsPerEm; }
    [[nodiscard]] unsigned int GetGlyphCount() const { return mGlyphCount; }
    [[nodiscard]] const VerticalMetrics& GetVerticalMetrics() const { return mVerticalMetrics; }

    /// <summary>
    /// 유니코드 코드포인트에 대응하는 글리프 번호다. 이 폰트에 없는 글자는 0(<c>.notdef</c>)이다.
    ///
    /// 0을 답하는 것과 실패하는 것은 다르다: 0은 「이 폰트에는 그 글자가 없다」는 <b>답</b>이고,
    /// 대체 폰트를 찾을지 두부를 그릴지는 부르는 쪽이 정한다.
    /// </summary>
    [[nodiscard]] std::uint16_t GetGlyphIndex(char32_t codePoint) const;

    /// <summary>글리프의 진행폭이다. 폰트 단위다.</summary>
    [[nodiscard]] std::uint16_t GetAdvanceWidth(std::uint16_t glyphId) const;

    /// <summary>이름으로 테이블의 바이트를 얻는다. 없으면 빈 span이다.</summary>
    [[nodiscard]] std::span<const std::byte> GetTable(std::string_view tag) const;

private:
    [[nodiscard]] bool ReadHead();
    [[nodiscard]] bool ReadMaxp();
    [[nodiscard]] bool ReadHorizontalMetrics();
    void ReadVerticalMetrics();
    [[nodiscard]] bool ReadCharacterMap();

    std::vector<std::byte> mBytes;
    std::unordered_map<std::string, std::span<const std::byte>> mTables;

    bool mValid = false;
    OutlineFormat mOutlineFormat = OutlineFormat::TrueType;
    unsigned int mUnitsPerEm = 0;
    unsigned int mGlyphCount = 0;
    VerticalMetrics mVerticalMetrics;

    /// <summary>hmtx가 진행폭을 몇 개 들고 있는가. 그 뒤의 글리프는 마지막 값을 나눠 쓴다.</summary>
    unsigned int mHorizontalMetricCount = 0;

    /// <summary>고른 cmap 부분표의 바이트다. format 4만 담는다.</summary>
    std::span<const std::byte> mCharacterMap;
};

}
