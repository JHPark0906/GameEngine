#include "pch.h"
#include "RectMask.h"

#include "RectTransform.h"

#include "PropertyDescriptor.h"

#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// RectMask가 선언하는 속성들이다 — 비어 있다. 자를 사각형은 이 오브젝트의 RectTransform이
    /// 이미 말하므로, 마스크가 파일에 적을 값이 없다. 붙어 있다는 사실 자체가 선언이다.
    /// </summary>
    std::span<const PropertyDescriptor> RectMaskProperties()
    {
        return {};
    }
}

namespace
{
    /// <summary>
    /// 화면 사각형 없이는 이 컴포넌트가 아무 일도 하지 않는다. 배치도 히트 테스트도 사각형으로
    /// 요소를 찾으므로, 사각형이 없으면 소리 없이 죽는다 — 붙일 때 함께 붙는 편이 낫다.
    /// </summary>
    [[nodiscard]] std::span<const ComponentType* const> RectMaskRequirements()
    {
        static const ComponentType* const required[]{ &RectTransform::StaticType() };
        return required;
    }
}

const ComponentType& RectMask::StaticType()
{
    static const ComponentType type{
        "RectMask", &Component::StaticType(), &RectMaskProperties,
        &MakeComponentInstance<RectMask>,
        &RectMaskRequirements };
    return type;
}

}
