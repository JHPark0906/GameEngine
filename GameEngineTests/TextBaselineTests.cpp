#include "TextBaselineTests.h"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "Text/CffFont.h"
#include "Text/FontFace.h"
#include "Text/GlyphRasterizer.h"
#include "Text/GlyphStructure.h"
#include "Text/TrueTypeOutlines.h"
#include "TestSupport.h"

using TestSupport::Expect;

/// <summary>
/// 우리 래스터라이저의 출력이 <b>지난번과 같은지</b>를 묻는다.
///
/// 이것은 「옳은가」를 묻지 않는다. 무엇이 옳은지는 플랫폼 렌더러와의 구조 비교가 따로 묻고,
/// 읽을 만한지는 사람이 화면에서 판정한다. 여기서 지키는 것은 그 둘 사이에 남는 것 —
/// <b>우리가 우리도 모르게 달라지지 않았는가</b> — 이고, 힌팅을 하지 않기로 한 이상 이것이
/// 자동으로 잡을 수 있는 유일한 종류의 회귀다.
///
/// 기준 그림은 <c>GameEngineTests/TextBaselines</c>에 있고, 그 디렉터리의 README가 언제
/// 어떻게 갱신하는지를 정한다. 갱신은 <b>환경 변수를 켜야만</b> 일어난다 — 붉어지면 스스로
/// 고치는 시험은 아무것도 보고하지 않는 시험이고, 그 실패는 조용하다.
/// </summary>
namespace
{
    struct BaselineCase
    {
        const char* fontFile;
        const char* fontName;
        char32_t codePoint;
        const char* characterName;
        float fontSize;
    };

    /// <summary>
    /// 글리프 이미지 기준을 두는 글자 크기들이다. 작은 크기의 11px도 포함한다.
    /// 기준 수를 작게 유지하여 변경된 이미지를 개별적으로 검토할 수 있게 한다.
    /// </summary>
    constexpr BaselineCase Cases[] = {
        { "D2Coding-Ver1.3.3-20260725.ttf", "D2Coding", U'A', "A", 11.0f },
        { "D2Coding-Ver1.3.3-20260725.ttf", "D2Coding", U'A', "A", 16.0f },
        { "D2Coding-Ver1.3.3-20260725.ttf", "D2Coding", U'가', "ga", 11.0f },
        { "D2Coding-Ver1.3.3-20260725.ttf", "D2Coding", U'가', "ga", 16.0f },
        { "MaruBuri-Regular.otf", "MaruBuri", U'A', "A", 11.0f },
        { "MaruBuri-Regular.otf", "MaruBuri", U'A', "A", 16.0f },
        { "MaruBuri-Regular.otf", "MaruBuri", U'가', "ga", 11.0f },
        { "MaruBuri-Regular.otf", "MaruBuri", U'가', "ga", 16.0f },
    };

    /// <summary>
    /// 환경 변수 하나를 읽는다. 없으면 빈 값이다.
    ///
    /// <c>std::getenv</c>는 표준이지만 MSVC가 거절하고, <c>_dupenv_s</c>는 MSVC에만 있다.
    /// 이 저장소에 환경 변수를 읽는 다른 자리가 없어 따를 관례도 없으므로, 갈림을 이 함수 하나에
    /// 가둔다 — 부르는 쪽은 어느 컴파일러인지 몰라도 된다.
    /// </summary>
    [[nodiscard]] std::string ReadEnvironmentVariable(const char* const name)
    {
#ifdef _MSC_VER
        char* value = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
        {
            return {};
        }
        std::string result(value);
        std::free(value);
        return result;
#else
        const char* const value = std::getenv(name);
        return value == nullptr ? std::string{} : std::string(value);
#endif
    }

    [[nodiscard]] std::filesystem::path TestsDirectory()
    {
        return std::filesystem::path(__FILE__).parent_path();
    }

    [[nodiscard]] std::filesystem::path FontDirectory()
    {
        return TestsDirectory().parent_path() / "GameEditor" / "Content" / "Fonts";
    }

    [[nodiscard]] std::filesystem::path BaselinePath(const BaselineCase& item)
    {
        std::ostringstream name;
        name << item.fontName << "-" << item.characterName << "-"
             << static_cast<int>(item.fontSize) << "px.pgm";
        return TestsDirectory() / "TextBaselines" / name.str();
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

    /// <summary>한 경우를 우리 래스터라이저로 그린다.</summary>
    [[nodiscard]] bool Render(const BaselineCase& item, GameEngine::Text::CoverageBitmap& bitmap)
    {
        using namespace GameEngine::Text;

        const std::vector<std::byte> bytes = ReadBytes(FontDirectory() / item.fontFile);
        FontFace face;
        if (bytes.empty() || !face.Parse(bytes))
        {
            return false;
        }
        const std::uint16_t glyphId = face.GetGlyphIndex(item.codePoint);
        if (glyphId == 0)
        {
            return false;
        }

        GlyphOutline outline;
        if (face.GetOutlineFormat() == FontFace::OutlineFormat::TrueType)
        {
            if (!GetTrueTypeGlyphOutline(face, glyphId, outline))
            {
                return false;
            }
        }
        else
        {
            CffFont cff;
            CharstringResult charstring;
            if (!cff.Parse(face.GetTable("CFF ")) || !cff.GetGlyphOutline(glyphId, charstring))
            {
                return false;
            }
            outline = std::move(charstring.outline);
        }

        const RasterizerSettings settings;
        return RasterizeGlyphOutline(
            outline, item.fontSize / static_cast<float>(face.GetUnitsPerEm()), settings, bitmap);
    }

    [[nodiscard]] std::string DescribeStructure(const GameEngine::Text::CoverageBitmap& bitmap)
    {
        const GameEngine::Text::GlyphStructure structure =
            GameEngine::Text::MeasureGlyphStructure(bitmap);
        std::ostringstream text;
        text << std::fixed << std::setprecision(2) << "box " << structure.GetWidth() << "x"
             << structure.GetHeight() << " at (" << structure.left << "," << structure.top
             << "), ink " << structure.inkArea << ", stroke " << structure.strokeWidth << "x"
             << structure.strokeHeight;
        return text.str();
    }

    /// <summary>
    /// 기준 그림을 쓴다. 사람이 읽는 머리말을 함께 담는다.
    ///
    /// 머리말의 <c>reason</c> 줄이 이 형식의 요점이다. 그림은 바뀌었는데 그 줄이 그대로면
    /// 「아무도 설명하지 않은 갱신」이고, 그것이 diff에 그대로 보인다.
    /// </summary>
    [[nodiscard]] bool WriteBaseline(
        const std::filesystem::path& path,
        const BaselineCase& item,
        const GameEngine::Text::CoverageBitmap& bitmap,
        const std::string_view existingReason)
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream stream(path, std::ios::binary);
        if (!stream)
        {
            return false;
        }
        stream << "P2\n";
        stream << "# glyph: " << item.fontName << " '" << item.characterName << "' at "
               << static_cast<int>(item.fontSize) << "px\n";
        stream << "# reason: "
               << (existingReason.empty() ? "first baseline" : existingReason) << "\n";
        stream << "# structure: " << DescribeStructure(bitmap) << "\n";
        stream << bitmap.width << " " << bitmap.height << "\n255\n";
        for (unsigned int y = 0; y < bitmap.height; ++y)
        {
            for (unsigned int x = 0; x < bitmap.width; ++x)
            {
                stream << static_cast<unsigned int>(static_cast<unsigned char>(
                              bitmap.alpha[static_cast<std::size_t>(y) * bitmap.width + x]))
                       << (x + 1 == bitmap.width ? '\n' : ' ');
            }
        }
        return static_cast<bool>(stream);
    }

    struct Baseline
    {
        unsigned int width = 0;
        unsigned int height = 0;
        std::vector<unsigned char> values;
        std::string reason;
        std::string structure;
        bool loaded = false;
    };

    [[nodiscard]] Baseline ReadBaseline(const std::filesystem::path& path)
    {
        Baseline baseline;
        std::ifstream stream(path);
        if (!stream)
        {
            return baseline;
        }
        std::string token;
        stream >> token;
        if (token != "P2")
        {
            return baseline;
        }
        // 주석 줄에서 머리말을 되찾는다. 실패해도 그림 비교는 되므로 없으면 비워 둔다.
        std::string line;
        std::getline(stream, line);
        while (stream.peek() == '#')
        {
            std::getline(stream, line);
            const auto colon = line.find(": ");
            if (colon == std::string::npos)
            {
                continue;
            }
            const std::string key = line.substr(2, colon - 2);
            const std::string value = line.substr(colon + 2);
            if (key == "reason")
            {
                baseline.reason = value;
            }
            else if (key == "structure")
            {
                baseline.structure = value;
            }
        }
        unsigned int maximum = 0;
        if (!(stream >> baseline.width >> baseline.height >> maximum))
        {
            return baseline;
        }
        baseline.values.reserve(
            static_cast<std::size_t>(baseline.width) * baseline.height);
        unsigned int value = 0;
        while (stream >> value)
        {
            baseline.values.push_back(static_cast<unsigned char>(value));
        }
        baseline.loaded =
            baseline.values.size() ==
            static_cast<std::size_t>(baseline.width) * baseline.height;
        return baseline;
    }
}

bool RunTextBaselineTests()
{
    using namespace GameEngine::Text;

    std::error_code error;
    if (!std::filesystem::is_directory(FontDirectory(), error) || error)
    {
        std::cout << "  text baseline tests skipped: the bundled fonts are not in this checkout\n";
        return true;
    }

    // 갱신은 환경 변수를 켜야만 일어난다. 붉어지면 스스로 고치는 시험은 아무것도 보고하지 않고,
    // 그 침묵은 눈치채기 어렵다 — 그림이 조금씩 흘러가는데 매 실행이 초록이다.
    const std::string updateRequest = ReadEnvironmentVariable("GAMEENGINE_UPDATE_TEXT_BASELINES");
    const bool updating = updateRequest == "1";

    bool passed = true;
    unsigned int compared = 0;
    for (const BaselineCase& item : Cases)
    {
        CoverageBitmap bitmap;
        if (!Expect(Render(item, bitmap), "the glyph rasterizes"))
        {
            passed = false;
            continue;
        }

        const std::filesystem::path path = BaselinePath(item);
        const Baseline baseline = ReadBaseline(path);

        if (updating)
        {
            const bool wrote = WriteBaseline(path, item, bitmap, baseline.reason);
            std::cout << "  " << (wrote ? "wrote" : "FAILED TO WRITE") << " "
                      << path.filename().string() << ": " << DescribeStructure(bitmap) << "\n";
            passed &= Expect(wrote, "the baseline is written when an update is asked for");
            continue;
        }

        if (!baseline.loaded)
        {
            std::cerr << "  " << path.filename().string()
                      << " is missing or unreadable. If this glyph is new, create it with "
                      << "GAMEENGINE_UPDATE_TEXT_BASELINES=1 and say why in the commit\n";
            passed &= Expect(false, "a baseline exists for every glyph under watch");
            continue;
        }

        ++compared;
        const bool sameSize =
            baseline.width == bitmap.width && baseline.height == bitmap.height;
        bool samePixels = sameSize;
        std::size_t firstDifference = 0;
        if (sameSize)
        {
            for (std::size_t index = 0; index < baseline.values.size(); ++index)
            {
                if (baseline.values[index] !=
                    static_cast<unsigned char>(bitmap.alpha[index]))
                {
                    samePixels = false;
                    firstDifference = index;
                    break;
                }
            }
        }

        if (!samePixels)
        {
            // 무엇이 달라졌는지를 <b>구조</b>로 말한다. 「픽셀 하나가 다르다」로는 의도한
            // 변화인지 알 수 없지만, 「획 두께가 2x5에서 3x5로」는 알 수 있다.
            std::cerr << "  " << path.filename().string() << " changed.\n"
                      << "      was: " << baseline.structure << "\n"
                      << "      now: " << DescribeStructure(bitmap) << "\n";
            if (!sameSize)
            {
                std::cerr << "      the bitmap is " << bitmap.width << "x" << bitmap.height
                          << ", was " << baseline.width << "x" << baseline.height << "\n";
            }
            else
            {
                std::cerr << "      first differing pixel at ("
                          << (firstDifference % bitmap.width) << ","
                          << (firstDifference / bitmap.width) << "): was "
                          << static_cast<unsigned int>(baseline.values[firstDifference])
                          << ", now "
                          << static_cast<unsigned int>(
                                 static_cast<unsigned char>(bitmap.alpha[firstDifference]))
                          << "\n";
            }
            std::cerr << "      the rasterizer draws this differently than it did. Find the "
                      << "change before updating the baseline; if it was wanted, update it and "
                      << "say why in the same commit\n";
        }
        passed &= Expect(samePixels, "the rasterizer draws this glyph as it did before");
    }

    if (!updating)
    {
        // 「견주었다」이지 「같았다」가 아니다. 달라진 것은 위에서 저마다 이름과 함께 붉어지므로,
        // 여기서 「변화 없음」이라 말하면 그 줄들과 어긋난 요약이 된다.
        std::cout << "  text baselines: " << compared << " glyph(s) compared\n";
        // 한 장도 견주지 못했는데 초록이면 이 검사는 아무것도 지키지 않은 것이다.
        passed &= Expect(compared > 0, "at least one baseline was actually compared");
    }
    return passed;
}

static const TestSupport::Registration gTextBaselineTests{
    "RenderCache", "text baseline tests should pass", RunTextBaselineTests };
