#include "pch.h"
#include "Canvas.h"

#include "PropertyDescriptor.h"

#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>Canvas가 스스로 선언하는 속성들이다.</summary>
    std::span<const PropertyDescriptor> CanvasProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<Canvas>(
                "scaleFactor", "Scale Factor",
                &Canvas::GetScaleFactor, &Canvas::SetScaleFactor),
        };
        return properties;
    }
}

const ComponentType& Canvas::StaticType()
{
    static const ComponentType type{
        "Canvas", &Component::StaticType(), &CanvasProperties,
        &MakeComponentInstance<Canvas> };
    return type;
}

}
