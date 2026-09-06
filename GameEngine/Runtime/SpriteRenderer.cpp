#include "pch.h"
#include "SpriteRenderer.h"

#include "PropertyDescriptor.h"
#include "SpriteAnimator.h"

#include <algorithm>
#include <memory>
#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// SpriteRenderer가 스스로 선언하는 속성들이다. 이름과 enum 문자열은 장면 파일 형식이므로
    /// 기존 파일이 읽히는 그대로이고, 공유 속성은 Renderer의 선언에서 사슬로 온다.
    /// </summary>
    std::span<const PropertyDescriptor> SpriteRendererProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeAssetProperty<SpriteRenderer>(
                "sprite", "Sprite", Assets::AssetType::Sprite, &SpriteRenderer::GetSprite, &SpriteRenderer::SetSprite,
                PropertyTraits::OmitWhenInvalid),
            MakeProperty<SpriteRenderer>(
                "size", "Size", &SpriteRenderer::GetSize, &SpriteRenderer::SetSize),
            MakeEnumProperty<SpriteRenderer>(
                "drawMode", "Draw Mode", { "simple", "sliced" },
                &SpriteRenderer::GetDrawMode, &SpriteRenderer::SetDrawMode),
            MakeEnumProperty<SpriteRenderer>(
                "space", "Space", { "world", "screen" },
                &SpriteRenderer::GetSpace, &SpriteRenderer::SetSpace),
            MakeProperty<SpriteRenderer>(
                "frame", "Frame", &SpriteRenderer::GetFrame, &SpriteRenderer::SetFrame),
            MakeProperty<SpriteRenderer>(
                "flipX", "Flip X", &SpriteRenderer::IsFlippedX, &SpriteRenderer::SetFlipX),
            MakeProperty<SpriteRenderer>(
                "flipY", "Flip Y", &SpriteRenderer::IsFlippedY, &SpriteRenderer::SetFlipY),
            MakeProperty<SpriteRenderer>(
                "color", "Color", &SpriteRenderer::GetColor, &SpriteRenderer::SetColor),
        };
        return properties;
    }
}

const ComponentType& SpriteRenderer::StaticType()
{
    static const ComponentType type{
        "SpriteRenderer", &Renderer::StaticType(), &SpriteRendererProperties,
        &MakeComponentInstance<SpriteRenderer> };
    return type;
}
void SpriteRenderer::SetSheetPingPongFrame(
    const float elapsedSeconds, const int firstFrame, const float frameRate, const bool loop)
{
    mFrame = firstFrame;
    mSheetPingPong = SheetPingPongSample{ elapsedSeconds, firstFrame, frameRate, loop };
}

int SpriteRenderer::ResolveFrame(const int sheetFrameCount) const
{
    if (!mSheetPingPong || sheetFrameCount <= 0)
    {
        return mFrame;
    }
    const auto& sample = *mSheetPingPong;
    return SelectAnimationFrame(sample.elapsedSeconds, sample.firstFrame,
        sheetFrameCount, sample.frameRate, sample.loop, true);
}

void SpriteRenderer::SetSize(const Math::Vector2& size)
{
    mSize = {
        (std::max)(size.GetX(), 0.001f),
        (std::max)(size.GetY(), 0.001f)};
}

}
