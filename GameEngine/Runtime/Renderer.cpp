#include "pch.h"
#include "Renderer.h"

#include "PropertyDescriptor.h"

#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// 렌더러들이 공유하는 속성이다. 파생 렌더러의 목록에는 기반 타입의 속성이
    /// 자기 속성보다 먼저 나타난다. 직렬화는 이 타입 사슬을 따라 공유 속성을 함께 읽고 쓴다.
    /// </summary>
    std::span<const PropertyDescriptor> RendererProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<Renderer>(
                "visible", "Visible", &Renderer::IsVisible, &Renderer::SetVisible),
            MakeProperty<Renderer>(
                "castShadows", "Cast Shadows",
                &Renderer::CastsShadows, &Renderer::SetCastShadows),
            MakeProperty<Renderer>(
                "receiveShadows", "Receive Shadows",
                &Renderer::ReceivesShadows, &Renderer::SetReceiveShadows),
            MakeProperty<Renderer>(
                "sortingOrder", "Sorting Order",
                &Renderer::GetSortingOrder, &Renderer::SetSortingOrder),
            MakeAssetProperty<Renderer>(
                "material", "Material", Assets::AssetType::Material, &Renderer::GetMaterial, &Renderer::SetMaterial,
                PropertyTraits::OmitWhenInvalid),
        };
        return properties;
    }
}

const ComponentType& Renderer::StaticType()
{
    static const ComponentType type{
        "Renderer", &Behaviour::StaticType(), &RendererProperties };
    return type;
}
}
