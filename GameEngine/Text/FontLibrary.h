#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "FontFace.h"

namespace GameEngine::Text
{

struct GlyphOutline;

/// <summary>
/// 두부 — 어느 폰트에도 없는 글자를 대신 그리는 네모다.
///
/// <b>폰트의 글리프 0을 쓰지 않는다.</b> 관례상 그 자리가 「모르는 글자」의 네모지만, 실제로는
/// 비어 있는 폰트가 흔하다 — 동봉된 D2Coding이 그렇고, 그대로 두면 없는 글자가 네모가 아니라
/// <b>아무것도 아닌 것</b>으로 나온다. 그러면 글자가 빠진 것과 공백이 화면에서 구별되지 않고,
/// 사람이 원인을 찾을 방법이 사라진다.
///
/// 그래서 네모를 우리가 만든다. 어떤 폰트를 등록하든 없는 글자는 반드시 보인다.
/// </summary>
/// <param name="unitsPerEm">그 폰트의 em 단위다. 네모를 글자 크기에 맞춘다.</param>
/// <returns>속이 빈 네모다. 바깥과 안쪽 고리가 반대로 감겨 테두리만 남는다.</returns>
[[nodiscard]] GlyphOutline MakeTofuOutline(unsigned int unitsPerEm);

/// <summary>
/// 등록된 폰트들과 각 글자를 그릴 face·글리프를 선택하는 규칙이다.
///
/// 요청한 패밀리가 없을 때와 선택한 face에 글자가 없을 때의 대체 규칙을 한 곳에서 정한다.
/// 배치와 래스터화는 같은 선택 결과를 사용한다.
///
/// 시스템 폰트는 찾지 않는다. 등록된 폰트만 사용해 폰트 선택이 플랫폼의 시스템 컬렉션에
/// 의존하지 않도록 한다.
/// </summary>
class FontLibrary final
{
public:
    /// <summary>고른 결과다. 무엇으로 그릴지와, 그것이 원했던 것인지를 함께 말한다.</summary>
    struct GlyphSelection
    {
        /// <summary>그릴 face다. 아무것도 등록되지 않았으면 null이다.</summary>
        const FontFace* face = nullptr;
        /// <summary>
        /// 그 face 안의 글리프 번호다. <see cref="isTofu"/>가 참이면 이 값은 뜻이 없고,
        /// 부르는 쪽은 대신 <see cref="MakeTofuOutline"/>을 그린다.
        /// </summary>
        std::uint16_t glyphId = 0;
        /// <summary>요청한 패밀리가 아니라 다른 폰트로 그리게 됐는가.</summary>
        bool substitutedFont = false;
        /// <summary>어느 등록 폰트에도 그 글자가 없어 두부를 그리는가.</summary>
        bool isTofu = false;

        /// <summary>
        /// 고른 face의 등록 번호다. 이 라이브러리 안에서만 뜻이 있으며 등록 순서를 따른다.
        ///
        /// 포인터가 아니라 번호를 함께 내는 이유는, 배치 결과가 <b>face의 정체</b>를 값으로
        /// 실어 나른 뒤 나중에 그것으로 같은 face를 다시 찾아야 하기 때문이다.
        /// </summary>
        std::size_t faceIndex = 0;
    };

    FontLibrary();
    ~FontLibrary();

    FontLibrary(const FontLibrary&) = delete;
    FontLibrary& operator=(const FontLibrary&) = delete;

    /// <summary>
    /// 폰트 파일의 바이트를 별명으로 등록한다. 같은 별명을 다시 등록하면 뒤의 것이 이긴다.
    /// </summary>
    /// <returns>읽어서 등록했으면 true다. 읽지 못한 파일은 등록되지 않는다.</returns>
    [[nodiscard]] bool Register(std::string_view familyAlias, std::span<const std::byte> fontBytes);

    [[nodiscard]] std::size_t GetRegisteredCount() const { return mFonts.size(); }

    /// <summary>별명으로 face를 찾는다. 없으면 null이다.</summary>
    [[nodiscard]] const FontFace* Find(std::string_view familyAlias) const;

    /// <summary>등록 번호로 face를 찾는다. 범위 밖이면 null이다.</summary>
    [[nodiscard]] const FontFace* GetFace(std::size_t faceIndex) const;

    /// <summary>
    /// 한 글자를 어느 face의 몇 번 글리프로 그릴지 정한다.
    ///
    /// 갈래는 이렇다. 패밀리가 비었거나 등록되지 않은 이름이면 <b>가장 먼저 등록된 폰트</b>로
    /// 보낸다 — 그러므로 <b>등록 순서가 답을 정한다</b>. 그 face에 글자가 없으면 나머지 등록
    /// 폰트를 등록 순서로 훑고, 어디에도 없으면 <b>두부</b>(글리프 0)를 그린다.
    ///
    /// 아무것도 등록되지 않았으면 <b>실패</b>다. 빈 글리프를 답하지 않는 이유는, 그것이 「글자가
    /// 없다」와 「폰트가 없다」를 같은 것으로 만들어 버리기 때문이다 — 앞은 두부로 보이고 뒤는
    /// 고쳐야 할 설정 문제다.
    /// </summary>
    /// <param name="familyAlias">요청한 패밀리다. 비어 있어도 된다.</param>
    /// <param name="codePoint">그릴 유니코드 코드포인트다.</param>
    /// <param name="selection">고른 face와 글리프를 받는다.</param>
    /// <returns>고를 수 있었으면 true다. 등록된 폰트가 하나도 없으면 false다.</returns>
    [[nodiscard]] bool SelectGlyph(
        std::string_view familyAlias, char32_t codePoint, GlyphSelection& selection);

private:
    struct Entry
    {
        std::string alias;
        std::unique_ptr<FontFace> face;
    };

    void WarnAboutFamilyOnce(std::string_view familyAlias, std::string_view drawnWith);
    void WarnAboutTofuOnce(char32_t codePoint);

    std::vector<Entry> mFonts;

    /// <summary>
    /// 이미 경고한 것들이다.
    ///
    /// 이 두 집합이 없으면 경고가 <b>프레임마다</b> 나온다 — 배치는 매 프레임 도는 자리이고,
    /// 그때 로그는 쓸모를 잃는 정도가 아니라 다른 모든 것을 밀어낸다.
    /// </summary>
    std::unordered_set<std::string> mWarnedFamilies;
    std::unordered_set<char32_t> mWarnedCodePoints;
};

}
