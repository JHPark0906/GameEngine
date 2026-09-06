#include "pch.h"
#include "ScrollRect.h"

#include "RectTransform.h"

#include "PropertyDescriptor.h"

#include <algorithm>
#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// ScrollRect가 선언하는 속성들이다. 밀린 거리도 여기 있다: 목록을 어디까지 내려 두었는지는
    /// 사람이 만든 상태라, 장면을 다시 열었을 때 보고 있던 자리에 있는 편이 낫다.
    /// </summary>
    std::span<const PropertyDescriptor> ScrollRectProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<ScrollRect>(
                "vertical", "Vertical", &ScrollRect::IsVertical, &ScrollRect::SetVertical),
            MakeProperty<ScrollRect>(
                "horizontal", "Horizontal", &ScrollRect::IsHorizontal,
                &ScrollRect::SetHorizontal),
            MakeProperty<ScrollRect>(
                "scrollOffset", "Scroll Offset", &ScrollRect::GetScrollOffset,
                &ScrollRect::SetScrollOffset),
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
    [[nodiscard]] std::span<const ComponentType* const> ScrollRectRequirements()
    {
        static const ComponentType* const required[]{ &RectTransform::StaticType() };
        return required;
    }
}

const ComponentType& ScrollRect::StaticType()
{
    static const ComponentType type{
        "ScrollRect", &Component::StaticType(), &ScrollRectProperties,
        &MakeComponentInstance<ScrollRect>,
        &ScrollRectRequirements };
    return type;
}

float ScrollRect::ClampAxis(
    const float offset, const float viewportExtent, const float contentExtent)
{
    // 넘치지 않으면 밀 곳이 없다. 음수 범위를 그대로 두면 짧은 목록이 화면 밖으로 밀린다.
    const float scrollable = (std::max)(contentExtent - viewportExtent, 0.0f);
    return std::clamp(offset, 0.0f, scrollable);
}

}
