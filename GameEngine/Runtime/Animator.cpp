#include "pch.h"
#include "Animator.h"

#include "PropertyDescriptor.h"

#include <cmath>
#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// Animator가 스스로 선언하는 속성이다. 공유 속성(material 등)은 Renderer의 선언에서
    /// 사슬로 온다.
    /// </summary>
    std::span<const PropertyDescriptor> AnimatorProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<Animator>(
                "playing", "Playing", &Animator::IsPlaying, &Animator::SetPlaying),
            MakeAssetProperty<Animator>(
                "mesh", "Skinned Mesh", Assets::AssetType::SkinnedMesh,
                &Animator::GetMesh, &Animator::SetMesh, PropertyTraits::OmitWhenInvalid),
            MakeAssetProperty<Animator>(
                "skeleton", "Skeleton", Assets::AssetType::Skeleton,
                &Animator::GetSkeleton, &Animator::SetSkeleton, PropertyTraits::OmitWhenInvalid),
            MakeAssetProperty<Animator>(
                "clip", "Clip", Assets::AssetType::AnimationClip,
                &Animator::GetClip, &Animator::SetClip, PropertyTraits::OmitWhenInvalid),
            MakeProperty<Animator>(
                "sortWithSprites", "Sort With Sprites",
                &Animator::SortsWithSprites, &Animator::SetSortWithSprites),
            MakeProperty<Animator>(
                "color", "Color", &Animator::GetColor, &Animator::SetColor),
        };
        return properties;
    }
}

const ComponentType& Animator::StaticType()
{
    static const ComponentType type{
        "Animator", &Renderer::StaticType(), &AnimatorProperties, &MakeComponentInstance<Animator> };
    return type;
}

void Animator::Restart()
{
    mElapsedSeconds = 0.0f;
}

void Animator::SetElapsedSeconds(const float elapsedSeconds)
{
    mElapsedSeconds = std::isfinite(elapsedSeconds) && elapsedSeconds >= 0.0f ? elapsedSeconds : 0.0f;
}

void Animator::UpdateBehaviour(const float deltaTime)
{
    if (mPlaying && std::isfinite(deltaTime) && deltaTime > 0.0f)
    {
        mElapsedSeconds += deltaTime;
    }
}

}
