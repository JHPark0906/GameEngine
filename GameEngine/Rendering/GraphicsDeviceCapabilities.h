#pragma once

namespace GameEngine::Rendering
{

/// <summary>
/// 그래픽 장치가 실제로 할 수 있는 것들이다. 그 장치가 스스로 보고한다.
///
/// 공유 상수가 아니다. 공유 정책처럼 읽히지만 한계는 장치의 속성이라, 공유 값이면 한계가 다른
/// API를 수용하려고 그 값을 모두를 위해 낮춰야 한다. 대신 장치가 지원하는 것을 보고하므로,
/// 한계가 더 작은 백엔드는 자기 자신만 제약한다.
/// </summary>
struct GraphicsDeviceCapabilities
{
    /// <summary>이 장치가 2D 텍스처에 허용하는 가장 긴 변이다. 텍셀 단위다.</summary>
    unsigned int maximumTextureDimension = 0;

    [[nodiscard]] bool IsValid() const { return maximumTextureDimension > 0; }
};

}
