#include "pch.h"
#include "UIWindow.h"

#include "PropertyDescriptor.h"

#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// UIWindow가 선언하는 속성들이다. 모달 여부는 그 창을 여는 코드가 정하는 한 프레임의
    /// 상태이지 파일에 적어 둘 값이 아니라서, 지금은 비어 있다.
    /// </summary>
    std::span<const PropertyDescriptor> UIWindowProperties()
    {
        return {};
    }
}

const ComponentType& UIWindow::StaticType()
{
    static const ComponentType type{
        "UIWindow", &Component::StaticType(), &UIWindowProperties,
        &MakeComponentInstance<UIWindow> };
    return type;
}

}
