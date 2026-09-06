#pragma once

#include <d3d12.h>

namespace GameEngine::Rendering::D3D12
{

/// <summary>지정한 힙 타입의 committed 리소스를 위한 최소한의 힙 속성이다.</summary>
[[nodiscard]] inline D3D12_HEAP_PROPERTIES HeapProperties(const D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties = {};
    properties.Type = type;
    return properties;
}

/// <summary>지정한 크기의 선형 버퍼에 대한 리소스 기술이다.</summary>
[[nodiscard]] inline D3D12_RESOURCE_DESC BufferDescription(const UINT64 size)
{
    D3D12_RESOURCE_DESC description = {};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}

}
