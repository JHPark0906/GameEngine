#include "pch.h"
#include "Light.h"

#include "PropertyDescriptor.h"

#include <algorithm>
#include <memory>
#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// Light가 선언하는 속성들이다. 직렬화 키, 인스펙터 행, enum 이름 표의 유일한 출처이며,
    /// 이름과 enum 문자열은 장면 파일 형식이므로 기존 파일이 읽히는 그대로다.
    /// </summary>
    std::span<const PropertyDescriptor> LightProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeEnumProperty<Light>(
                "kind", "Kind", { "directional", "point", "ambient" },
                &Light::GetKind, &Light::SetKind),
            MakeProperty<Light>("color", "Color", &Light::GetColor, &Light::SetColor),
            MakeProperty<Light>(
                "intensity", "Intensity", &Light::GetIntensity, &Light::SetIntensity),
            MakeProperty<Light>("range", "Range", &Light::GetRange, &Light::SetRange),
        };
        return properties;
    }
}

const ComponentType& Light::StaticType()
{
    static const ComponentType type{
        "Light", &Behaviour::StaticType(), &LightProperties, &MakeComponentInstance<Light> };
    return type;
}

void Light::SetIntensity(const float intensity)
{
    mIntensity = (std::max)(intensity, 0.0f);
}

void Light::SetRange(const float range)
{
    mRange = (std::max)(range, 0.001f);
}

}
