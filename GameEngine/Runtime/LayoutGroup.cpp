#include "pch.h"
#include "LayoutGroup.h"

#include "PropertyDescriptor.h"
#include "RectTransform.h"

#include <algorithm>
#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>LayoutGroup이 선언하는 속성들이다. 직렬화 키와 인스펙터 행의 유일한 출처다.</summary>
    std::span<const PropertyDescriptor> LayoutGroupProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeEnumProperty<LayoutGroup>(
                "direction", "Direction", { "horizontal", "vertical" },
                &LayoutGroup::GetDirection, &LayoutGroup::SetDirection),
            MakeProperty<LayoutGroup>(
                "spacing", "Spacing", &LayoutGroup::GetSpacing, &LayoutGroup::SetSpacing),
            MakeProperty<LayoutGroup>(
                "padding", "Padding", &LayoutGroup::GetPadding, &LayoutGroup::SetPadding),
        };
        return properties;
    }

    /// <summary>
    /// 화면 사각형 없이는 이 컴포넌트가 아무 일도 하지 않는다. 구한 크기를 적어 둘 자리가
    /// 사각형이기 때문이다.
    /// </summary>
    [[nodiscard]] std::span<const ComponentType* const> LayoutGroupRequirements()
    {
        static const ComponentType* const required[]{ &RectTransform::StaticType() };
        return required;
    }
}

const ComponentType& LayoutGroup::StaticType()
{
    static const ComponentType type{
        "LayoutGroup", &Component::StaticType(), &LayoutGroupProperties,
        &MakeComponentInstance<LayoutGroup>,
        &LayoutGroupRequirements };
    return type;
}

void LayoutGroup::SetSpacing(const float spacing)
{
    mSpacing = (std::max)(spacing, 0.0f);
}

void LayoutGroup::SetPadding(const float padding)
{
    mPadding = (std::max)(padding, 0.0f);
}

float LayoutGroup::MeasureAlong(
    const float alongAxis, const int childCount, const float spacing, const float padding)
{
    const int count = (std::max)(childCount, 0);
    if (count == 0)
    {
        return 2.0f * padding;
    }
    // 간격은 자식 사이에만 들어간다: n개면 n-1군데다.
    return 2.0f * padding + (std::max)(alongAxis, 0.0f) +
        static_cast<float>(count - 1) * spacing;
}

float LayoutGroup::MeasureAcross(const float acrossAxis, const float padding)
{
    return 2.0f * padding + (std::max)(acrossAxis, 0.0f);
}

}
