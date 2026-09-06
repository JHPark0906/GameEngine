#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "RenderFrame.h"
#include "../Platform/ITextRasterizer.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 한 문자열이 아틀라스 글리프들로 배치된 결과의 한 페이지 몫이다. 문자열의 글리프가 여러
/// 페이지에 흩어졌으면 run이 여러 개고, TextDraw는 run 하나에서 하나씩 만들어진다.
/// </summary>
struct ShapedTextRun
{
    std::shared_ptr<const RasterizedTextImage> page;
    std::shared_ptr<const std::vector<TextGlyphQuad>> glyphs;
};

/// <summary>
/// 한 문자열의 완성된 배치다: 페이지별 글리프 quad들과 블록 크기다. 프레임이 싣는 완성 데이터
/// 그대로이며, 호출자는 블록 크기로 텍스트를 배치하고 잰다.
/// </summary>
struct ShapedText
{
    std::vector<ShapedTextRun> runs;
    unsigned int width = 0;
    unsigned int height = 0;

    std::vector<Platform::TextCaretStop> caretStops;

    /// <summary>잉크가 전혀 없는 문자열 — 공백뿐인 텍스트 — 은 run이 없는 유효한 배치다.</summary>
    [[nodiscard]] bool IsValid() const { return width > 0 && height > 0; }
};

/// <summary>
/// 프론트엔드를 위해 문자열을 아틀라스 글리프 배치로 바꾸고 최근 결과를 보관한다.
///
/// 레이아웃만 문자열 단위로 다시 하고 글리프 픽셀은 GlyphAtlas가 폰트·크기·글리프 단위로
/// 재사용하므로, 바뀐 문자열의 비용은 "처음 보는 글리프 몇 개"다. 문자열 전체를 한 장의
/// 이미지로 래스터화하면 문자열이 바뀔 때마다 전체 재래스터와 백엔드 재업로드를 치르고, 매
/// 프레임 갱신되는 텍스트가 백엔드 텍스처·descriptor 캐시를 고갈시킨다.
///
/// 캐시는 공용 텍스트 예산으로 제한되고 frame-bound 규칙을 지킨다: 현재 프레임에 건네준
/// 배치는 결코 퇴거되지 않는다. 호출자도 shared_ptr를 쥐고 페이지도 shared_ptr이므로, 퇴거나
/// 아틀라스 재시작이 in-flight 프레임이 아직 참조하는 것을 무효화하는 일은 없다.
/// </summary>
class TextRasterizationCache final
{
public:
    /// <summary>
    /// 이 캐시가 쓸 텍스트 래스터라이저를 받는다. 렌더링은 플랫폼 구현을 고르지 않는다: 무엇을
    /// 쓸지는 합성 루트가 정하고, 그래서 텍스트 없이 도는 시험은 null을 건네면 된다.
    ///
    /// 받는 것은 아직 초기화되지 않은 래스터라이저이고, 초기화는 첫 요청까지 미뤄진다. 폰트
    /// 스택을 여는 비용은 그때 치러지므로, 텍스트가 없는 프로젝트는 여전히 치르지 않는다.
    /// </summary>
    /// <param name="rasterizer">
    /// 초기화되지 않은 플랫폼 래스터라이저다. null이면 이 캐시는 텍스트를 배치하지 않는다.
    /// </param>
    explicit TextRasterizationCache(std::unique_ptr<Platform::ITextRasterizer> rasterizer);
    ~TextRasterizationCache();

    TextRasterizationCache(const TextRasterizationCache&) = delete;
    TextRasterizationCache& operator=(const TextRasterizationCache&) = delete;

    /// <summary>새 프레임의 사용 수명을 시작하고, 더 오래된 프레임의 배치가 퇴거될 수 있게 한다.</summary>
    void BeginFrame();

    /// <summary>
    /// 폰트 파일의 바이트를 별명으로 등록해, 이후 요청이 fontFamily로 그 별명을 쓸 수 있게
    /// 한다. 플랫폼 래스터라이저가 아직 초기화되지 않았으면 여기서 초기화한다.
    /// </summary>
    /// <param name="familyAlias">요청이 이 폰트를 부를 이름이다.</param>
    /// <param name="fontBytes">폰트 파일 전체의 바이트다.</param>
    /// <returns>
    /// 등록에 성공했으면 true다. 실패하면 그 별명의 요청은 빈 fontFamily와 같은 대접을 받는다
    /// — 그것을 무엇으로 그릴지는 실제로 만들어진 래스터라이저 구현이 정한다.
    /// </returns>
    [[nodiscard]] bool RegisterFont(
        std::string_view familyAlias, std::span<const std::byte> fontBytes);

    /// <summary>
    /// 요청을 배치하거나, 이전에 배치된 요청이면 캐시된 결과를 반환한다. 래스터라이저가
    /// 텍스트를 만들 수 없으면 null을 반환하므로, 호출자는 빈 draw를 백엔드에 넘기는 대신
    /// 그 draw를 버린다.
    /// </summary>
    [[nodiscard]] std::shared_ptr<const ShapedText> Resolve(
        const Platform::TextRasterizationRequest& request);

    /// <summary>
    /// 지금 보관 중인 배치의 수다. 예산(<see cref="TextCacheBudget"/>)이 항목 수로
    /// 세어지므로, 이 값이 곧 예산을 얼마나 쓰고 있는지다.
    ///
    /// 진단을 위해 있다. 같은 문자열을 재고 그리는 두 경로가 정말 한 항목만 쓰는지는
    /// 짐작이 아니라 세어서 답해야 하는 질문이고, 세지 못하면 그 답은 다음 사람에게도
    /// 짐작으로 남는다.
    /// </summary>
    /// <returns>보관 중인 배치의 수다.</returns>
    [[nodiscard]] std::size_t GetShapedEntryCount() const;

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

}
