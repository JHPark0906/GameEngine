#pragma once

#include <d3d12.h>

#include "../IGraphicsDevice.h"

namespace GameEngine::Rendering::D3D12
{

/// <summary>D3D12 렌더 패스가 백엔드 전용 장치 상태를 조회하는 인터페이스이다.</summary>
class ID3D12GraphicsDevice : public IGraphicsDevice
{
public:
    virtual ~ID3D12GraphicsDevice() = default;

    [[nodiscard]] virtual ID3D12Device* GetDevice() const = 0;

    /// <summary>현재 프레임을 기록 중인 command list이다. BeginFrame과 EndFrame 사이에만 유효하다.</summary>
    [[nodiscard]] virtual ID3D12GraphicsCommandList* GetCommandList() const = 0;

};

}
