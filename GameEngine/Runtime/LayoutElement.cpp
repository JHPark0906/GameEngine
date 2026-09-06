#include "pch.h"
#include "LayoutElement.h"

#include "RectTransform.h"

#include "PropertyDescriptor.h"

#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>LayoutElement가 선언하는 속성들이다. 직렬화 키와 인스펙터 행의 유일한 출처다.</summary>
    std::span<const PropertyDescriptor> LayoutElementProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeEnumProperty<LayoutElement>(
                "fit", "Fit", { "none", "horizontal", "vertical", "both" },
                &LayoutElement::GetFit, &LayoutElement::SetFit),
            MakeProperty<LayoutElement>(
                "wrap", "Wrap",
                &LayoutElement::IsWrapping, &LayoutElement::SetWrapping),
            MakeProperty<LayoutElement>(
                "padding", "Padding",
                &LayoutElement::GetPadding, &LayoutElement::SetPadding),
            MakeProperty<LayoutElement>(
                "minimumSize", "Minimum Size",
                &LayoutElement::GetMinimumSize, &LayoutElement::SetMinimumSize),
        };
        return properties;
    }
}

namespace
{
    /// <summary>
    /// 화면 사각형 없이는 이 컴포넌트가 아무 일도 하지 않는다. 배치도 히트 테스트도 사각형으로
    /// 요소를 찾으므로, 사각형이 없으면 소리 없이 죽는다 — 붙일 때 함께 붙는 편이 낫다.
    /// </summary>
    [[nodiscard]] std::span<const ComponentType* const> LayoutElementRequirements()
    {
        static const ComponentType* const required[]{ &RectTransform::StaticType() };
        return required;
    }
}

const ComponentType& LayoutElement::StaticType()
{
    static const ComponentType type{
        "LayoutElement", &Component::StaticType(), &LayoutElementProperties,
        &MakeComponentInstance<LayoutElement>,
        &LayoutElementRequirements };
    return type;
}

}
