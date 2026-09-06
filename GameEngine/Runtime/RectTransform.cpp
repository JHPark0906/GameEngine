#include "pch.h"
#include "RectTransform.h"

#include "PropertyDescriptor.h"

#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// RectTransform이 스스로 선언하는 속성들이다. 계산된 사각형은 여기 없다 — 그것은 선언이
    /// 아니라 선언의 결과이고, 매 프레임 다시 만들어지므로 장면 파일에 실릴 것이 아니다.
    /// </summary>
    std::span<const PropertyDescriptor> RectTransformProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<RectTransform>(
                "anchorMin", "Anchor Min",
                &RectTransform::GetAnchorMin, &RectTransform::SetAnchorMin),
            MakeProperty<RectTransform>(
                "anchorMax", "Anchor Max",
                &RectTransform::GetAnchorMax, &RectTransform::SetAnchorMax),
            MakeProperty<RectTransform>(
                "offsetMin", "Offset Min",
                &RectTransform::GetOffsetMin, &RectTransform::SetOffsetMin),
            MakeProperty<RectTransform>(
                "offsetMax", "Offset Max",
                &RectTransform::GetOffsetMax, &RectTransform::SetOffsetMax),
        };
        return properties;
    }
}

const ComponentType& RectTransform::StaticType()
{
    static const ComponentType type{
        "RectTransform", &Component::StaticType(), &RectTransformProperties,
        &MakeComponentInstance<RectTransform> };
    return type;
}

}
