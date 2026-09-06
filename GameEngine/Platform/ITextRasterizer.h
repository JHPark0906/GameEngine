#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GameEngine::Platform
{

/// <summary>래스터화된 텍스트 블록의 가로 정렬이다.</summary>
enum class TextAlignment : unsigned char
{
    Left,
    Center,
    Right,
};

/// <summary>
/// 글리프로 배치할 문자열이다. 렌더링 계층의 draw 타입과는 일부러 독립적이라, 플랫폼
/// 계층은 결코 Rendering에 의존하지 않는다.
/// </summary>
struct TextRasterizationRequest
{
    /// <summary>UTF-8 텍스트이다. 줄바꿈을 담을 수 있다.</summary>
    std::string text;
    /// <summary>
    /// 폰트 패밀리 이름이다. 비어 있거나 등록되지 않은 이름일 때 무엇을 고르는지는 구현마다
    /// 다르다 — 각 <c>ITextRasterizer</c> 구현의 문서를 본다.
    /// </summary>
    std::string fontFamily;
    /// <summary>폰트 두께이다. 100(가늘게)에서 950(굵게)이며 400이 보통이다.</summary>
    int fontWeight = 400;
    float fontSize = 32.0f;
    /// <summary>줄바꿈 폭이다. 픽셀 단위이며, 0은 명시적 줄바꿈 폭이 없다는 뜻이다.</summary>
    float maxWidth = 0.0f;
    /// <summary>폰트 크기의 배수로 나타낸 줄 높이이다.</summary>
    float lineSpacing = 1.0f;
    TextAlignment alignment = TextAlignment::Left;
};

/// <summary>
/// 레이아웃이 배치한 글리프 하나다. 좌표는 텍스트 블록 좌상단 원점의 픽셀이며, 글리프의 배치
/// 원점 — 베이스라인 위의 펜 위치 — 을 가리킨다. 비트맵이 그 원점에서 얼마나 떨어져 그려지는지는
/// 글리프 래스터화가 답한다.
/// </summary>
struct PositionedGlyph
{
    /// <summary>폰트 face 안의 글리프 번호다. 문자 코드가 아니라 셰이핑이 고른 결과다.</summary>
    std::uint16_t glyphId = 0;
    float x = 0.0f;
    float y = 0.0f;
};

/// <summary>
/// 한 폰트 face와 크기로 이어 그려지는 글리프 묶음이다. fontKey는 레이아웃이 실제로 고른
/// face — 요청한 패밀리가 못 그리는 글자를 대체 폰트가 맡은 경우를 포함한다 — 의 정체성이며,
/// 글리프 래스터화가 같은 키로 같은 face를 다시 찾는다.
/// </summary>
struct PositionedGlyphRun
{
    std::uint64_t fontKey = 0;
    float fontSize = 0.0f;
    std::vector<PositionedGlyph> glyphs;
};

/// <summary>
/// 문자열 하나의 레이아웃 결과다: 줄바꿈·정렬·커닝·문자 조합이 끝난 글리프 배치와 블록 크기다.
/// </summary>
/// <summary>UTF-8 문자 경계의 실제 배치 좌표다. 블록 좌상단 기준 픽셀이며 공백의 진행폭도 포함한다.</summary>
struct TextCaretStop
{
    std::size_t byteOffset = 0;
    float x = 0.0f;
    float y = 0.0f;
    float height = 0.0f;
};

struct TextGlyphLayout
{
    std::vector<PositionedGlyphRun> runs;
    unsigned int width = 0;
    unsigned int height = 0;
    std::vector<TextCaretStop> caretStops;
};

/// <summary>
/// 글리프 하나의 커버리지 비트맵이다. 픽셀당 1바이트, 위에서 아래 순서다. 잉크가 없는 글리프 —
/// 공백 — 는 너비와 높이가 0이며 그것도 성공이다. offset은 글리프 배치 원점에서 비트맵
/// 좌상단까지의 픽셀 거리다.
/// </summary>
struct RasterizedGlyph
{
    std::vector<std::byte> alphaPixels;
    unsigned int width = 0;
    unsigned int height = 0;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
};

/// <summary>
/// 텍스트를 배치하고 글리프를 커버리지 픽셀로 바꾸는 인터페이스다. 모든 그래픽
/// 백엔드가 하나의 구현을 공유하므로, 줄바꿈·정렬·줄 간격·문자 조합이 백엔드 간에 동일하다.
///
/// 문자열 전체를 한 장의 이미지로 만드는 대신 레이아웃과 글리프 래스터화가 나뉘어 있다:
/// 레이아웃은 문자열이 바뀔 때마다 다시 하지만, 글리프 픽셀은 아틀라스가 폰트·크기·글리프
/// 단위로 캐시하므로 같은 글자를 다시 그리는 비용이 사라진다.
/// </summary>
class ITextRasterizer
{
public:
    virtual ~ITextRasterizer() = default;

    ITextRasterizer(const ITextRasterizer&) = delete;
    ITextRasterizer& operator=(const ITextRasterizer&) = delete;
    ITextRasterizer(ITextRasterizer&&) = delete;
    ITextRasterizer& operator=(ITextRasterizer&&) = delete;

    /// <summary>래스터라이저를 준비한다. 레이아웃과 래스터화 전에 한 번 호출한다.</summary>
    [[nodiscard]] virtual bool Initialize() = 0;

    /// <summary>
    /// 폰트 파일의 바이트를 별명으로 등록한다. 그 뒤로 요청의 fontFamily가 이 별명이면 시스템
    /// 폰트 대신 이 파일의 폰트로 배치한다.
    ///
    /// 파일 경로가 아니라 바이트를 받는 이유는 패키징된 빌드에 디스크 파일이 없기 때문이다:
    /// 폰트는 프로젝트 콘텐츠이고, 콘텐츠는 IContentSource가 어디에 있든 바이트로 내준다.
    /// 같은 별명을 다시 등록하면 뒤의 것이 이긴다.
    /// </summary>
    /// <param name="familyAlias">요청이 이 폰트를 부를 이름이다. 비어 있으면 실패다.</param>
    /// <param name="fontBytes">폰트 파일 전체의 바이트다. 구현이 자기 사본을 쥔다.</param>
    /// <returns>폰트를 읽어 등록했으면 true다. 실패하면 별명은 등록되지 않는다.</returns>
    [[nodiscard]] virtual bool RegisterFont(
        std::string_view familyAlias, std::span<const std::byte> fontBytes) = 0;

    /// <summary>요청이 이 래스터라이저의 입력 한계 계약 안에 드는지 여부이다.</summary>
    [[nodiscard]] virtual bool IsSupported(const TextRasterizationRequest& request) const = 0;

    /// <summary>
    /// 지원되는 요청을 배치된 글리프로 레이아웃한다. 실패하면 false를 반환하고 결과를 비워
    /// 둔다. 결과의 fontKey들은 이 래스터라이저 인스턴스 안에서만 유효하다.
    /// </summary>
    /// <param name="request">배치할 문자열과 폰트·줄바꿈 설정이다.</param>
    /// <param name="layout">배치된 글리프 런들과 블록 크기를 받는다.</param>
    /// <returns>레이아웃에 성공했으면 true이다.</returns>
    [[nodiscard]] virtual bool LayoutText(
        const TextRasterizationRequest& request, TextGlyphLayout& layout) = 0;

    /// <summary>
    /// 레이아웃이 보고한 face에서 글리프 하나를 래스터화한다. 잉크 없는 글리프는 크기 0의
    /// 성공이다.
    /// </summary>
    /// <param name="fontKey">글리프를 소유한 face의 정체성이다. LayoutText 결과에서 온다.</param>
    /// <param name="fontSize">레이아웃과 같은 폰트 크기다. 픽셀 단위다.</param>
    /// <param name="glyphId">face 안의 글리프 번호다.</param>
    /// <param name="result">커버리지 픽셀과 배치 오프셋을 받는다.</param>
    /// <returns>글리프를 만들었으면 true이다. 모르는 fontKey는 실패다.</returns>
    [[nodiscard]] virtual bool RasterizeGlyph(
        std::uint64_t fontKey,
        float fontSize,
        std::uint16_t glyphId,
        RasterizedGlyph& result) = 0;

protected:
    ITextRasterizer() = default;
};

}
