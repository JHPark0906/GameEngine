#include "FontLibraryTests.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "Diagnostics/Debug.h"
#include "Text/CffFont.h"
#include "Text/FontLibrary.h"
#include "Text/GlyphRasterizer.h"
#include "Text/TrueTypeOutlines.h"
#include "TestSupport.h"

using TestSupport::Expect;

/// <summary>
/// 「이 글자를 무엇으로 그릴 것인가」의 두 갈래와, 그 끝에 있는 두부를 확인한다.
///
/// 사용자가 정한 답이라 여기서 지켜야 할 것이 셋이다. 등록된 폰트가 하나도 없으면 <b>실패</b>여야
/// 하고(빈 글자와 구별되어야 한다), 경고는 <b>같은 것에 대해 한 번만</b> 나야 하며(배치는 매
/// 프레임 도는 자리다), 그리고 두부는 <b>실제로 그려져야</b> 한다 — 「아무것도 안 그려진다」와
/// 「네모가 그려진다」는 다르고, 사용자가 화면에서 원인을 찾을 수 있으려면 후자여야 한다.
/// </summary>
namespace
{
    [[nodiscard]] std::filesystem::path FontDirectory()
    {
        return std::filesystem::path(__FILE__).parent_path().parent_path() / "GameEditor" /
            "Content" / "Fonts";
    }

    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            return {};
        }
        const std::streamoff size = stream.tellg();
        if (size <= 0)
        {
            return {};
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(bytes.data()), size);
        return stream ? bytes : std::vector<std::byte>{};
    }

    /// <summary>경고를 세는 동안만 붙어 있는 수신기다.</summary>
    class WarningCounter final
    {
    public:
        WarningCounter()
        {
            mListener = GameEngine::Diagnostics::Debug::AddLogListener(
                [this](const GameEngine::Diagnostics::LogEntry& entry)
                {
                    if (entry.level == GameEngine::Diagnostics::LogLevel::Warning)
                    {
                        ++mCount;
                    }
                });
        }

        ~WarningCounter() { GameEngine::Diagnostics::Debug::RemoveLogListener(mListener); }

        WarningCounter(const WarningCounter&) = delete;
        WarningCounter& operator=(const WarningCounter&) = delete;

        [[nodiscard]] unsigned int Count() const { return mCount; }
        void Reset() { mCount = 0; }

    private:
        GameEngine::Diagnostics::Debug::LogListenerId mListener = 0;
        unsigned int mCount = 0;
    };

    /// <summary>고른 글리프를 실제로 그려 잉크가 있는지 본다.</summary>
    [[nodiscard]] bool HasInk(
        const GameEngine::Text::FontLibrary::GlyphSelection& selection, const float fontSize)
    {
        using namespace GameEngine::Text;
        if (!selection.face)
        {
            return false;
        }

        GlyphOutline outline;
        if (selection.isTofu)
        {
            // 두부는 폰트에서 읽지 않고 우리가 만든다. 폰트의 글리프 0이 비어 있는 일이 흔해서다.
            outline = MakeTofuOutline(selection.face->GetUnitsPerEm());
        }
        else if (selection.face->GetOutlineFormat() == FontFace::OutlineFormat::TrueType)
        {
            if (!GetTrueTypeGlyphOutline(*selection.face, selection.glyphId, outline))
            {
                return false;
            }
        }
        else
        {
            CffFont cff;
            CharstringResult charstring;
            if (!cff.Parse(selection.face->GetTable("CFF ")) ||
                !cff.GetGlyphOutline(selection.glyphId, charstring))
            {
                return false;
            }
            outline = std::move(charstring.outline);
        }

        CoverageBitmap bitmap;
        const RasterizerSettings settings;
        if (!RasterizeGlyphOutline(
                outline, fontSize / static_cast<float>(selection.face->GetUnitsPerEm()), settings,
                bitmap))
        {
            return false;
        }
        for (const std::byte value : bitmap.alpha)
        {
            if (static_cast<unsigned char>(value) > 0)
            {
                return true;
            }
        }
        return false;
    }
}

bool RunFontLibraryTests()
{
    using namespace GameEngine::Text;

    const std::filesystem::path coding = FontDirectory() / "D2Coding-Ver1.3.3-20260725.ttf";
    const std::filesystem::path maru = FontDirectory() / "MaruBuri-Regular.otf";
    std::error_code error;
    if (!std::filesystem::is_regular_file(coding, error) || error ||
        !std::filesystem::is_regular_file(maru, error) || error)
    {
        std::cout << "  font library tests skipped: the bundled fonts are not in this checkout\n";
        return true;
    }

    bool passed = true;

    // ⑴ 등록된 폰트가 하나도 없으면 실패다. 빈 글리프를 답하면 「글자가 없다」와 「폰트가 없다」가
    // 같아 보이고, 뒤쪽은 고쳐야 할 설정 문제다.
    {
        FontLibrary empty;
        FontLibrary::GlyphSelection selection;
        passed &= Expect(
            !empty.SelectGlyph("anything", U'A', selection),
            "with no font registered at all, selecting a glyph fails rather than drawing nothing");
    }

    FontLibrary library;
    passed &= Expect(
        library.Register("console", ReadBytes(coding)), "a TrueType font registers");
    passed &= Expect(library.Register("body", ReadBytes(maru)), "and a CFF font registers");
    passed &= Expect(
        !library.Register("broken", std::vector<std::byte>(64, std::byte{ 0x11 })),
        "and a file that is not a font is refused rather than registered empty");
    passed &= Expect(library.GetRegisteredCount() == 2, "so two fonts are registered");

    // ⑵ 빈 이름과 모르는 이름은 둘 다 가장 먼저 등록된 폰트로 간다. 시스템 폰트는 찾지 않는다.
    {
        WarningCounter warnings;
        FontLibrary::GlyphSelection viaEmpty;
        FontLibrary::GlyphSelection viaUnknown;
        passed &= Expect(
            library.SelectGlyph("", U'A', viaEmpty) &&
                library.SelectGlyph("no-such-family", U'A', viaUnknown),
            "an empty family and an unknown family both resolve");
        passed &= Expect(
            viaEmpty.face == library.Find("console") &&
                viaUnknown.face == library.Find("console"),
            "and both are drawn with the first font that was registered");
        passed &= Expect(
            viaEmpty.substitutedFont && viaUnknown.substitutedFont,
            "and both say they are not the font that was asked for");

        // 같은 패밀리를 여러 번 물어도 경고는 한 번이다. 배치는 매 프레임 도는 자리라, 여기서
        // 반복하면 로그가 쓸모를 잃는 정도가 아니라 다른 모든 것을 밀어낸다.
        const unsigned int afterTwoFamilies = warnings.Count();
        for (int repeat = 0; repeat < 5; ++repeat)
        {
            FontLibrary::GlyphSelection again;
            static_cast<void>(library.SelectGlyph("no-such-family", U'B', again));
        }
        std::cout << "  warnings: " << afterTwoFamilies << " for two unknown families, "
                  << (warnings.Count() - afterTwoFamilies) << " more after five repeats\n";
        passed &= Expect(
            afterTwoFamilies == 2,
            "each family that is not registered warns, so the cause can be found");
        passed &= Expect(
            warnings.Count() == afterTwoFamilies,
            "and warns only the first time, because layout runs every frame");
    }

    // ⑶ 요청한 face에 글자가 없으면 등록된 다른 폰트로 넘어간다 — 등록된 것들 사이에서만.
    {
        FontLibrary::GlyphSelection selection;
        // 한글은 두 폰트에 다 있으므로, 대체가 일어나는지는 「요청한 쪽에 없는 글자」로 봐야 한다.
        // 그래서 여기서는 요청한 face에 있는 글자로 대체가 <b>일어나지 않음</b>을 확인한다.
        passed &= Expect(
            library.SelectGlyph("body", U'가', selection) &&
                selection.face == library.Find("body") && !selection.substitutedFont &&
                !selection.isTofu,
            "a character the requested font has is drawn by that font, with no substitution");
    }

    // ⑷ 어느 폰트에도 없는 글자는 두부다. 그리고 두부는 <b>그려져야</b> 한다.
    {
        WarningCounter warnings;
        // 사용자 정의 영역이다. 본문 폰트가 채워 둘 이유가 없는 자리다.
        constexpr char32_t Missing = static_cast<char32_t>(0xE000);
        FontLibrary::GlyphSelection selection;
        passed &= Expect(
            library.SelectGlyph("console", Missing, selection),
            "a character no registered font has still resolves");
        passed &= Expect(selection.isTofu, "and says it is a box");

        // 폰트가 들고 있는 글리프 0이 실제로 무엇인지 적어 둔다. 이 값이 「우리가 네모를 직접
        // 만드는」 이유이고, 나중에 폰트가 바뀌어도 다시 재지 않게 남긴다.
        for (const std::string_view alias : { "console", "body" })
        {
            const FontFace* const face = library.Find(alias);
            if (!face)
            {
                continue;
            }
            FontLibrary::GlyphSelection asGlyphZero;
            asGlyphZero.face = face;
            asGlyphZero.glyphId = 0;
            std::cout << "  " << alias << "'s own glyph 0 has ink: "
                      << (HasInk(asGlyphZero, 32.0f) ? "yes" : "no") << "\n";
        }

        // 🔴 여기가 사용자 조건의 핵심이다. 「아무것도 안 그려진다」가 아니라 「네모가 그려진다」여야
        // 한다 — 화면에 네모가 보여야 사람이 폰트가 빠졌다는 것을 알 수 있다.
        const bool inked = HasInk(selection, 32.0f);
        if (!inked)
        {
            std::cerr << "  the tofu glyph rasterizes to nothing, so a missing character would "
                      << "be invisible instead of showing a box\n";
        }
        passed &= Expect(inked, "and it actually draws a box rather than nothing at all");

        const unsigned int afterFirst = warnings.Count();
        for (int repeat = 0; repeat < 5; ++repeat)
        {
            FontLibrary::GlyphSelection again;
            static_cast<void>(library.SelectGlyph("console", Missing, again));
        }
        std::cout << "  tofu warnings: " << afterFirst << " first, "
                  << (warnings.Count() - afterFirst) << " more after five repeats\n";
        passed &= Expect(afterFirst == 1, "the missing character is reported once");
        passed &= Expect(
            warnings.Count() == afterFirst, "and not again for the same character");
    }

    // ⑸ 등록 순서가 답을 정한다. 같은 별명을 다시 등록해도 그 순서는 바뀌지 않는다.
    {
        passed &= Expect(
            library.Register("console", ReadBytes(coding)),
            "registering the same alias again succeeds");
        FontLibrary::GlyphSelection selection;
        passed &= Expect(
            library.SelectGlyph("", U'A', selection) && selection.face == library.Find("console"),
            "and the first-registered font is still the one an unknown family falls back to");
        passed &= Expect(
            library.GetRegisteredCount() == 2, "and no duplicate entry was added");
    }
    return passed;
}

static const TestSupport::Registration gFontLibraryTests{
    "RenderCache", "font library tests should pass", RunFontLibraryTests };
