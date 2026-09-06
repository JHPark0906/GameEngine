#pragma once

#include <memory>

#include "IWindow.h"

namespace GameEngine::Platform
{

/// <summary>
/// 이 빌드가 겨냥한 플랫폼의 창 구현을 만든다. 엔진에서 구체 창 클래스의 이름을 부르는 유일한
/// 자리이므로, App은 Win32가 아니라 IWindow에 의존한다.
/// </summary>
class WindowFactory final
{
public:
    [[nodiscard]] static std::unique_ptr<IWindow> Create();
};

}
