#include "pch.h"
#include "FontLibrary.h"

#include "../Diagnostics/Debug.h"
#include "GlyphOutline.h"

#include <algorithm>

namespace GameEngine::Text
{

GlyphOutline MakeTofuOutline(const unsigned int unitsPerEm)
{
    // 대문자 높이쯤에 맞춘 네모다. 글자들 사이에 섞였을 때 크기가 튀지 않으면서, 빠진 자리라는
    // 것은 분명히 보이는 비율이다.
    const auto em = static_cast<float>(unitsPerEm);
    const float left = em * 0.08f;
    const float right = em * 0.62f;
    const float bottom = 0.0f;
    const float top = em * 0.70f;
    // 테두리 두께다. 획이 너무 얇으면 작은 크기에서 사라지고, 너무 두꺼우면 속이 안 보인다.
    const float thickness = em * 0.06f;

    const auto rectangle = [](const float x0, const float y0, const float x1, const float y1,
                              const bool clockwise)
    {
        OutlineContour contour;
        contour.start = OutlinePoint{ x0, y0 };
        if (clockwise)
        {
            contour.segments.push_back(PromoteLine({ x0, y0 }, { x0, y1 }));
            contour.segments.push_back(PromoteLine({ x0, y1 }, { x1, y1 }));
            contour.segments.push_back(PromoteLine({ x1, y1 }, { x1, y0 }));
            contour.segments.push_back(PromoteLine({ x1, y0 }, { x0, y0 }));
        }
        else
        {
            contour.segments.push_back(PromoteLine({ x0, y0 }, { x1, y0 }));
            contour.segments.push_back(PromoteLine({ x1, y0 }, { x1, y1 }));
            contour.segments.push_back(PromoteLine({ x1, y1 }, { x0, y1 }));
            contour.segments.push_back(PromoteLine({ x0, y1 }, { x0, y0 }));
        }
        return contour;
    };

    GlyphOutline outline;
    // 두 고리를 <b>반대로</b> 감는다. 0이 아닌 감김수 규칙에서 그것이 속을 비우는 방법이고,
    // 같은 방향으로 감으면 속이 찬 네모가 되어 글자처럼 보이지 않는다.
    outline.contours.push_back(rectangle(left, bottom, right, top, false));
    outline.contours.push_back(rectangle(
        left + thickness, bottom + thickness, right - thickness, top - thickness, true));
    return outline;
}

FontLibrary::FontLibrary() = default;
FontLibrary::~FontLibrary() = default;

bool FontLibrary::Register(
    const std::string_view familyAlias, const std::span<const std::byte> fontBytes)
{
    if (familyAlias.empty())
    {
        return false;
    }

    auto face = std::make_unique<FontFace>();
    if (!face->Parse(fontBytes))
    {
        return false;
    }

    const auto existing = std::ranges::find_if(
        mFonts, [familyAlias](const Entry& entry) { return entry.alias == familyAlias; });
    if (existing != mFonts.end())
    {
        // 같은 별명을 다시 등록하면 뒤의 것이 이긴다. 자리는 그대로 두어 등록 순서를 지킨다 —
        // 그 순서가 대체 폰트의 답을 정하므로, 다시 등록하는 것만으로 순서가 바뀌면 안 된다.
        existing->face = std::move(face);
        return true;
    }
    mFonts.push_back(Entry{ std::string(familyAlias), std::move(face) });
    return true;
}

const FontFace* FontLibrary::Find(const std::string_view familyAlias) const
{
    const auto found = std::ranges::find_if(
        mFonts, [familyAlias](const Entry& entry) { return entry.alias == familyAlias; });
    return found == mFonts.end() ? nullptr : found->face.get();
}

const FontFace* FontLibrary::GetFace(const std::size_t faceIndex) const
{
    return faceIndex < mFonts.size() ? mFonts[faceIndex].face.get() : nullptr;
}

void FontLibrary::WarnAboutFamilyOnce(
    const std::string_view familyAlias, const std::string_view drawnWith)
{
    std::string key(familyAlias);
    if (!mWarnedFamilies.insert(key).second)
    {
        return;
    }
    Diagnostics::Debug::LogWarning(
        "A text font family that is not registered was asked for, so another font is drawing it. "
        "requested=",
        familyAlias.empty() ? std::string_view("(none)") : familyAlias, ", drawn with=", drawnWith);
}

void FontLibrary::WarnAboutTofuOnce(const char32_t codePoint)
{
    if (!mWarnedCodePoints.insert(codePoint).second)
    {
        return;
    }
    Diagnostics::Debug::LogWarning(
        "No registered font has this character, so it is drawn as a box. codePoint=U+",
        static_cast<unsigned int>(codePoint));
}

bool FontLibrary::SelectGlyph(
    const std::string_view familyAlias, const char32_t codePoint, GlyphSelection& selection)
{
    selection = GlyphSelection{};
    if (mFonts.empty())
    {
        // 등록된 폰트가 없다. 빈 글리프를 답하면 「글자가 없다」와 구별할 수 없게 되므로 실패로
        // 답한다 — 이쪽은 고쳐야 할 설정 문제다.
        return false;
    }

    const FontFace* requested = Find(familyAlias);
    std::size_t requestedIndex = 0;
    for (std::size_t index = 0; index < mFonts.size(); ++index)
    {
        if (mFonts[index].face.get() == requested)
        {
            requestedIndex = index;
            break;
        }
    }
    if (!requested)
    {
        // 빈 이름과 모르는 이름은 같은 답을 받는다: 가장 먼저 등록된 폰트다. 시스템 폰트는
        // 찾지 않는다.
        requested = mFonts.front().face.get();
        requestedIndex = 0;
        selection.substitutedFont = true;
        WarnAboutFamilyOnce(familyAlias, mFonts.front().alias);
    }

    if (const std::uint16_t glyphId = requested->GetGlyphIndex(codePoint); glyphId != 0)
    {
        selection.face = requested;
        selection.glyphId = glyphId;
        selection.faceIndex = requestedIndex;
        return true;
    }

    // 요청한 face에 그 글자가 없다. 나머지를 등록 순서로 훑는다 — 시스템 전체가 아니라
    // 등록된 것들 사이에서만이다.
    for (std::size_t index = 0; index < mFonts.size(); ++index)
    {
        const Entry& entry = mFonts[index];
        if (entry.face.get() == requested)
        {
            continue;
        }
        if (const std::uint16_t glyphId = entry.face->GetGlyphIndex(codePoint); glyphId != 0)
        {
            selection.face = entry.face.get();
            selection.glyphId = glyphId;
            selection.faceIndex = index;
            selection.substitutedFont = true;
            return true;
        }
    }

    // 아무도 갖고 있지 않다. 두부를 그린다: 글리프 0은 폰트가 「모르는 글자」를 위해 들고 있는
    // 네모다. 아무것도 안 그리는 것과는 다르며, 화면에 네모가 보여야 사람이 원인을 찾을 수 있다.
    selection.face = requested;
    selection.glyphId = 0;
    selection.faceIndex = requestedIndex;
    selection.isTofu = true;
    WarnAboutTofuOnce(codePoint);
    return true;
}

}
