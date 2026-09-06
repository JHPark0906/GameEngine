#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "../Platform/ITextRasterizer.h"
#include "FontLibrary.h"

namespace GameEngine::Text
{

/// <summary>
/// 엔진의 텍스트 배치와 글리프 래스터화 구현이다. 플랫폼 폰트 스택을 쓰지 않으며
/// <see cref="Platform::ITextRasterizer"/> 계약을 따른다.
///
/// 폰트 선택은 FontLibrary, 줄바꿈은 줄바꿈기, 글자 모양은 각 형식의 윤곽선 파서와
/// 래스터라이저가 담당한다. 이 클래스는 진행폭, 줄 간격, 정렬을 적용해 글리프를 배치한다.
///
/// 힌팅을 하지 않으므로 작은 글자의 획이 힌팅을 사용하는 렌더러와 다를 수 있다.
/// </summary>
class EngineTextRasterizer final : public Platform::ITextRasterizer
{
public:
    EngineTextRasterizer();
    ~EngineTextRasterizer() override;

    [[nodiscard]] bool Initialize() override;

    [[nodiscard]] bool RegisterFont(
        std::string_view familyAlias, std::span<const std::byte> fontBytes) override;

    [[nodiscard]] bool IsSupported(
        const Platform::TextRasterizationRequest& request) const override;

    [[nodiscard]] bool LayoutText(
        const Platform::TextRasterizationRequest& request,
        Platform::TextGlyphLayout& layout) override;

    [[nodiscard]] bool RasterizeGlyph(
        std::uint64_t fontKey,
        float fontSize,
        std::uint16_t glyphId,
        Platform::RasterizedGlyph& result) override;

private:
    FontLibrary mFonts;
};

}
