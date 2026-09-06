#include "pch.h"
#include "TextRenderer.h"

#include "PropertyDescriptor.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// TextRenderer가 스스로 선언하는 속성들이다. 이름과 enum 문자열은 장면 파일 형식이므로
    /// 기존 파일이 읽히는 그대로이고, 공유 속성은 Renderer의 선언에서 사슬로 온다. enum 이름
    /// 표의 순서는 직렬화 기본값이 아니라 열거자 순서를 따른다 — 인덱스가 곧 값이다.
    /// </summary>
    std::span<const PropertyDescriptor> TextRendererProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<TextRenderer>(
                "text", "Text", &TextRenderer::GetText, &TextRenderer::SetText),
            MakeProperty<TextRenderer>(
                "fontFamily", "Font Family",
                &TextRenderer::GetFontFamily, &TextRenderer::SetFontFamily),
            MakeProperty<TextRenderer>(
                "fontSize", "Font Size", &TextRenderer::GetFontSize, &TextRenderer::SetFontSize),
            MakeProperty<TextRenderer>(
                "maxWidth", "Max Width", &TextRenderer::GetMaxWidth, &TextRenderer::SetMaxWidth),
            MakeProperty<TextRenderer>(
                "lineSpacing", "Line Spacing",
                &TextRenderer::GetLineSpacing, &TextRenderer::SetLineSpacing),
            MakeProperty<TextRenderer>(
                "pixelsPerUnit", "Pixels Per Unit",
                &TextRenderer::GetPixelsPerUnit, &TextRenderer::SetPixelsPerUnit),
            MakeProperty<TextRenderer>(
                "color", "Color", &TextRenderer::GetColor, &TextRenderer::SetColor),
            MakeProperty<TextRenderer>(
                "backgroundColor", "Background Color",
                &TextRenderer::GetBackgroundColor, &TextRenderer::SetBackgroundColor),
            MakeProperty<TextRenderer>(
                "backgroundPadding", "Backdrop Padding",
                &TextRenderer::GetBackgroundPadding, &TextRenderer::SetBackgroundPadding),
            MakeEnumProperty<TextRenderer>(
                "space", "Space", { "world", "screen" },
                &TextRenderer::GetSpace, &TextRenderer::SetSpace),
            MakeEnumProperty<TextRenderer>(
                "alignment", "Alignment", { "left", "center", "right" },
                &TextRenderer::GetAlignment, &TextRenderer::SetAlignment),
            MakeEnumProperty<TextRenderer>(
                "verticalAlignment", "Vertical Alignment", { "top", "middle", "bottom" },
                &TextRenderer::GetVerticalAlignment, &TextRenderer::SetVerticalAlignment),
        };
        return properties;
    }
}

const ComponentType& TextRenderer::StaticType()
{
    static const ComponentType type{
        "TextRenderer", &Renderer::StaticType(), &TextRendererProperties,
        &MakeComponentInstance<TextRenderer> };
    return type;
}
void TextRenderer::SetFontFamily(std::string fontFamily)
{
    mFontFamily = fontFamily.empty() ? "Segoe UI" : std::move(fontFamily);
}

void TextRenderer::SetFontSize(const float fontSize)
{
    mFontSize = std::clamp(fontSize, 1.0f, 256.0f);
}

void TextRenderer::SetMaxWidth(const float maxWidth)
{
    mMaxWidth = (std::max)(maxWidth, 0.0f);
}

void TextRenderer::SetLineSpacing(const float lineSpacing)
{
    mLineSpacing = (std::max)(lineSpacing, 0.1f);
}

void TextRenderer::SetPixelsPerUnit(const float pixelsPerUnit)
{
    mPixelsPerUnit = (std::max)(pixelsPerUnit, 0.001f);
}

void TextRenderer::SetBackgroundPadding(const Math::Vector2& padding)
{
    mBackgroundPadding = {
        std::isfinite(padding.GetX()) ? (std::max)(padding.GetX(), 0.0f) : 0.0f,
        std::isfinite(padding.GetY()) ? (std::max)(padding.GetY(), 0.0f) : 0.0f };
}

}
