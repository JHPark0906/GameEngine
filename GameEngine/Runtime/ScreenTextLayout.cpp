#include "pch.h"
#include "ScreenTextLayout.h"

#include "Canvas.h"
#include "GameObject.h"
#include "LayoutElement.h"
#include "TextRenderer.h"
#include "Transform.h"

namespace GameEngine::Runtime
{

float GetTextCanvasScale(const GameObject& object)
{
    for (const Transform* node = &object.GetTransform(); node; node = node->GetParent())
        if (const auto* owner = node->GetGameObject())
            if (const auto* canvas = owner->GetComponent<Canvas>()) return canvas->GetScaleFactor();
    return 1.0f;
}

Platform::TextRasterizationRequest MakeScreenTextRequest(
    const GameObject& object, const TextRenderer& renderer)
{
    const float scale = GetTextCanvasScale(object);
    Platform::TextRasterizationRequest request;
    request.text = renderer.GetText();
    request.fontFamily = renderer.GetFontFamily();
    request.fontSize = renderer.GetFontSize() * scale;
    request.lineSpacing = renderer.GetLineSpacing();
    request.maxWidth = renderer.GetMaxWidth();
    request.alignment = static_cast<Platform::TextAlignment>(renderer.GetAlignment());
    if (const auto* layout = object.GetComponent<LayoutElement>(); layout && layout->IsWrapping())
        if (const auto* rect = object.GetComponent<RectTransform>())
        {
            const float width = rect->GetResolvedRect().width - layout->GetPadding().GetX() * scale;
            request.maxWidth = width > 0.0f ? width : 0.0f;
        }
    return request;
}

Math::Vector2 GetScreenTextOrigin(
    const TextRenderer& renderer, const RectTransform::Rect& rect, const float width, const float height)
{
    const float xFactor = renderer.GetAlignment() == TextRenderer::Alignment::Center ? .5f :
        renderer.GetAlignment() == TextRenderer::Alignment::Right ? 1.0f : 0.0f;
    const float yFactor = renderer.GetVerticalAlignment() == TextRenderer::VerticalAlignment::Middle ? .5f :
        renderer.GetVerticalAlignment() == TextRenderer::VerticalAlignment::Bottom ? 1.0f : 0.0f;
    return { rect.x + (rect.width - width) * xFactor, rect.y + (rect.height - height) * yFactor };
}

}
