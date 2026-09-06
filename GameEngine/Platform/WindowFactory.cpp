#include "pch.h"
#include "WindowFactory.h"

// The one place in the engine that names a concrete window implementation. Porting to another
// windowing system means adding its IWindow implementation and one branch here.
#include "Win32/Win32Window.h"

#include <memory>

namespace GameEngine::Platform
{

std::unique_ptr<IWindow> WindowFactory::Create()
{
    return std::make_unique<Win32::Win32Window>();
}

}
