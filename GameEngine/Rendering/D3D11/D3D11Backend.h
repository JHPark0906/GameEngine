#pragma once

#include "../GraphicsBackend.h"

namespace GameEngine::Rendering::D3D11
{

/// <summary>그래픽 백엔드 레지스트리에 Direct3D 11 백엔드를 기술한다.</summary>
[[nodiscard]] GraphicsBackendDescriptor GetGraphicsBackendDescriptor();

}
