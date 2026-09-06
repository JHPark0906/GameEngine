#pragma once

#include <d3d11.h>

#include "../IGraphicsDevice.h"

namespace GameEngine::Rendering::D3D11
{

/// <summary>D3D11 렌더 패스가 백엔드 전용 장치 상태를 조회하는 인터페이스이다.</summary>
class ID3D11GraphicsDevice : public IGraphicsDevice
{
public:
    virtual ~ID3D11GraphicsDevice() = default;

    [[nodiscard]] virtual ID3D11Device* GetDevice() const = 0;
    [[nodiscard]] virtual ID3D11DeviceContext* GetDeviceContext() const = 0;
};

}
