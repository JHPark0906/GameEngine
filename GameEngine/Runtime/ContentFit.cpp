#include "pch.h"
#include "ContentFit.h"

#include "RectTransform.h"

#include "PropertyDescriptor.h"

#include <algorithm>
#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>ContentFit이 선언하는 속성들이다. 직렬화 키와 인스펙터 행의 유일한 출처다.</summary>
    std::span<const PropertyDescriptor> ContentFitProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeEnumProperty<ContentFit>(
                "direction", "Direction", { "vertical", "horizontal" },
                &ContentFit::GetDirection, &ContentFit::SetDirection),
            MakeProperty<ContentFit>(
                "itemSize", "Item Size", &ContentFit::GetItemSize, &ContentFit::SetItemSize),
            MakeProperty<ContentFit>(
                "spacing", "Spacing", &ContentFit::GetSpacing, &ContentFit::SetSpacing),
            MakeProperty<ContentFit>(
                "padding", "Padding", &ContentFit::GetPadding, &ContentFit::SetPadding),
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
    [[nodiscard]] std::span<const ComponentType* const> ContentFitRequirements()
    {
        static const ComponentType* const required[]{ &RectTransform::StaticType() };
        return required;
    }
}

const ComponentType& ContentFit::StaticType()
{
    static const ComponentType type{
        "ContentFit", &Component::StaticType(), &ContentFitProperties,
        &MakeComponentInstance<ContentFit>,
        &ContentFitRequirements };
    return type;
}

void ContentFit::SetItemSize(const float itemSize)
{
    // 음수 크기는 사각형이 아니다. 뒤집힌 선언을 흘리면 받는 쪽마다 다르게 어긋난다.
    mItemSize = (std::max)(itemSize, 0.0f);
}

void ContentFit::SetSpacing(const float spacing)
{
    mSpacing = (std::max)(spacing, 0.0f);
}

void ContentFit::SetPadding(const float padding)
{
    mPadding = (std::max)(padding, 0.0f);
}

float ContentFit::MeasureExtent(const int childCount) const
{
    const int count = (std::max)(childCount, 0);
    if (count == 0)
    {
        return 2.0f * mPadding;
    }
    // 간격은 자식 사이에만 들어간다: n개면 n-1군데다.
    return 2.0f * mPadding + static_cast<float>(count) * mItemSize +
        static_cast<float>(count - 1) * mSpacing;
}

}
