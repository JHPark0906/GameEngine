#pragma once

#include <cstring>
#include <DirectXMath.h>

#include "../../Math/Matrix.h"
#include "../ShaderInterop.h"

namespace GameEngine::Rendering::Direct3D
{

/// <summary>
/// 엔진 행렬을 DirectXMath 대응물로 변환한다. 두 타입 모두 행 벡터 규약을 쓰므로, 원소 순서를
/// 전치 없이 그대로 복사한다.
/// </summary>
[[nodiscard]] inline DirectX::XMMATRIX ToDirectXMatrix(const Math::Matrix4x4& matrix)
{
    const DirectX::XMFLOAT4X4 value{
        matrix.GetElement(0, 0), matrix.GetElement(0, 1), matrix.GetElement(0, 2), matrix.GetElement(0, 3),
        matrix.GetElement(1, 0), matrix.GetElement(1, 1), matrix.GetElement(1, 2), matrix.GetElement(1, 3),
        matrix.GetElement(2, 0), matrix.GetElement(2, 1), matrix.GetElement(2, 2), matrix.GetElement(2, 3),
        matrix.GetElement(3, 0), matrix.GetElement(3, 1), matrix.GetElement(3, 2), matrix.GetElement(3, 3)
    };
    return DirectX::XMLoadFloat4x4(&value);
}

/// <summary>
/// 엔진 행렬을 HLSL이 기대하는 행 순서로 공유 상수 레이아웃에 저장한다.
///
/// 전치는 Direct3D 계열의 사정이고, 그래서 공유 레이아웃이 아니라 여기서 일어난다: 다른 셰이딩
/// 언어는 행을 엔진이 이미 가진 그대로 원할 수도 있다.
/// </summary>
inline void StoreTransposed(ShaderFloat4x4& destination, const Math::Matrix4x4& source)
{
    DirectX::XMFLOAT4X4 stored;
    DirectX::XMStoreFloat4x4(&stored, DirectX::XMMatrixTranspose(ToDirectXMatrix(source)));
    static_assert(
        sizeof(stored) == sizeof(destination.elements),
        "A DirectXMath matrix and a shared shader matrix must hold the same sixteen floats.");
    std::memcpy(destination.elements.data(), &stored, sizeof(stored));
}

}
