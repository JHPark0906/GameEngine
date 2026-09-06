#include "pch.h"
#include "MeshRenderer.h"

#include "PropertyDescriptor.h"

#include <memory>
#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// MeshRenderer가 스스로 선언하는 속성이다. 공유 속성은 Renderer의 선언에서 사슬로 온다.
    /// </summary>
    std::span<const PropertyDescriptor> MeshRendererProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeAssetProperty<MeshRenderer>(
                "mesh", "Mesh", Assets::AssetType::Mesh, &MeshRenderer::GetMesh, &MeshRenderer::SetMesh,
                PropertyTraits::OmitWhenInvalid),
            MakeProperty<MeshRenderer>(
                "sortWithSprites", "Sort With Sprites",
                &MeshRenderer::SortsWithSprites, &MeshRenderer::SetSortWithSprites),
            MakeProperty<MeshRenderer>(
                "color", "Color", &MeshRenderer::GetColor, &MeshRenderer::SetColor),
        };
        return properties;
    }
}

const ComponentType& MeshRenderer::StaticType()
{
    static const ComponentType type{
        "MeshRenderer", &Renderer::StaticType(), &MeshRendererProperties,
        &MakeComponentInstance<MeshRenderer> };
    return type;
}
}
