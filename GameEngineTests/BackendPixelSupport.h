#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Assets/TextureData.h"
#include "Math/Color.h"
#include "Rendering/RenderFrame.h"

namespace GameEngine::Rendering
{
struct GraphicsBackendDescriptor;
}

namespace TestSupport
{

/// <summary>
/// 그려진 이미지에서 픽셀을 읽는 검사들이 함께 쓰는 것들이다.
///
/// <see cref="TestSupport"/>의 본체와 파일을 나눈 이유는 여기가 렌더링 헤더를 필요로 하기
/// 때문이다. 이것을 TestSupport.h에 두면 픽셀을 읽지 않는 시험까지 렌더링 전체를 딸려
/// 컴파일하게 된다.
/// </summary>
struct Rgba
{
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    unsigned char a = 0;

    [[nodiscard]] bool operator==(const Rgba&) const = default;
};

/// <summary>캡처한 이미지의 한 픽셀이다. 오프셋은 이미지가 말하는 픽셀당 바이트로 잡는다.</summary>
[[nodiscard]] Rgba ReadPixel(
    const GameEngine::Rendering::CapturedImage& image, unsigned int x, unsigned int y);

/// <summary>사람이 읽는 "(r, g, b, a)" 한 줄이다.</summary>
[[nodiscard]] std::string Describe(const Rgba& color);

/// <summary>
/// 바이트로 나타낸 색이다.
///
/// 채널이 온전한 8비트 값인 색에 쓰면, 비교가 하드웨어의 반올림에 대한 추측이 아니라 프레임이
/// 요구한 값에 대해 이뤄진다.
/// </summary>
[[nodiscard]] Rgba Quantize(const GameEngine::Math::Color& color);

/// <summary>
/// 이 기계가 실제로 세울 수 있는 백엔드들이다.
///
/// 이름으로 D3D11과 D3D12를 부르는 대신 등록부를 훑으므로, 나중에 더해진 백엔드도 같은 요구를
/// 아무 편집 없이 함께 받는다. 비어 있으면 이 기계는 그 질문에 답할 수 없는 기계다.
/// </summary>
[[nodiscard]] std::vector<const GameEngine::Rendering::GraphicsBackendDescriptor*>
SupportedBackends();

/// <summary>
/// 위 절반과 아래 절반의 색이 다른 8x8 그림이다.
/// 수직 방향을 검증할 때 단색 이미지는 뒤집힘을 구분할 수 없으므로 사용하지 않는다.
/// </summary>
[[nodiscard]] std::shared_ptr<const GameEngine::Assets::TextureData> MakeHalvedTexture(
    std::uint64_t id, const Rgba& upper, const Rgba& lower);

/// <summary>
/// 네 귀퉁이가 배경색으로 파인 정사각 그림이다. 나머지는 <paramref name="fill"/>로 채운다.
///
/// 파인 자리를 투명이 아니라 배경색으로 칠하는 까닭은, 투명으로 두면 그 자리가 보이느냐가 알파
/// 블렌딩에 달리고 블렌딩은 렌더 패스마다 다르기 때문이다 — 그러면 검사가 조각의 자리가 아니라
/// 블렌딩 설정을 묻게 된다.
///
/// 반지름은 테두리와 같게 두는 것을 전제로 한다. 반지름이 테두리보다 작으면 파이는 자리가 몇
/// 픽셀로 쪼그라들어, 조각이 뒤집혀도 옮겨 갈 자리가 없어 아무 단언도 그것을 보지 못한다.
/// </summary>
[[nodiscard]] std::shared_ptr<const GameEngine::Assets::TextureData> MakeRoundedTexture(
    std::uint64_t id, unsigned int extent, float cornerRadius, const Rgba& fill,
    const Rgba& carved);

}
