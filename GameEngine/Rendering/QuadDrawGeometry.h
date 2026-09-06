#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "../Math/Color.h"
#include "../Math/Matrix.h"
#include "RenderFrame.h"

namespace GameEngine::Rendering
{

/// <summary>quad가 표시하는 resolve된 텍스처의 픽셀 크기이다.</summary>
struct QuadTextureSize
{
    unsigned int width = 0;
    unsigned int height = 0;
};

/// <summary>
/// 텍스처 입힌 quad 하나가 어디에 놓이고 어떻게 틴트되는지이다. 엔진의 표준 클립 공간 기준이다.
///
/// 스프라이트나 텍스트 한 줄을 그리는 데 모든 백엔드에 필요한 것이 정확히 이것이고, 그 어느
/// 것도 그래픽 API에 의존하지 않는다: pixels-per-unit 배율, flip, 스크린 공간 투영, 그리고
/// 프레임이 이미 기술하는 카메라 변환이다. 여기서 함께 계산해 모든 백엔드가 같은
/// 프레임의 기하를 일관되게 배치한다.
/// </summary>
struct QuadTransform
{
    Math::Matrix4x4 worldViewProjection;
    Math::Color tint = Math::Color::White;
    /// <summary>xy는 UV 배율, zw는 UV 오프셋이다. 뒤집힌 스프라이트는 이것으로 역방향 샘플링한다.</summary>
    std::array<float, 4> uvTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
};

/// <summary>
/// 스프라이트 quad를 배치하거나, 프레임이 그것을 기술할 수 없는 이유를 보고한다. 스프라이트는
/// 언제나 프레임의 카메라를 쓰고 프론트엔드가 그것을 보장하므로, 카메라가 없다는 것은 백엔드가
/// 자기 투영으로 덮어 가릴 일이 아니라 계약 위반이다.
/// </summary>
/// <param name="backendName">거부를 보고하는 백엔드 이름이다. 예: "D3D11".</param>
[[nodiscard]] std::optional<QuadTransform> TryBuildSpriteQuad(
    const RenderFrame& frame,
    const SpriteDraw& draw,
    QuadTextureSize texture,
    const char* backendName);

/// <summary>
/// nine-slice 스프라이트의 quad들을 배치하고 그 개수를 반환한다. 프레임이 기술할 수 없으면
/// 0이다. 모서리는 border가 말한 크기를 지키고, 가장자리는 자기 방향으로 늘어나며, 가운데가
/// 나머지를 채운다. border가 자리를 남기지 않는 cell은 그냥 만들지 않으며, 그래서 개수가
/// 달라진다.
///
/// 다른 quad 배치 옆에 있는 이유는 같은 종류의 일이기 때문이다: 여기 있는 모든 것이 프레임이
/// 이미 실어 온 것에 대한 산술이라, 모든 백엔드가 동일한 quad를 받고 백엔드에는 slicing이라는
/// 개념 자체가 필요 없다 — 하나를 그리듯 최대 아홉 개를 그릴 뿐이다.
/// </summary>
/// <param name="backendName">거부를 보고하는 백엔드 이름이다. 예: "D3D11".</param>
[[nodiscard]] std::size_t BuildSlicedSpriteQuads(
    const RenderFrame& frame,
    const SpriteDraw& draw,
    QuadTextureSize texture,
    const char* backendName,
    std::array<QuadTransform, 9>& quads);

/// <summary>
/// 텍스트 draw의 공통 기저다: 공간에 맞는 투영과 배율이다. 글리프 quad들은 이 기저 위에 하나씩
/// 놓인다.
/// </summary>
struct TextQuadBasis
{
    Math::Matrix4x4 viewProjection;
    /// <summary>로컬 픽셀을 draw의 공간 단위로 바꾸는 배율이다. 스크린은 1, 월드는 1/ppu다.</summary>
    float scale = 1.0f;
    /// <summary>스크린 공간이면 참이다. 로컬 +y가 화면 위인지 아래인지가 여기서 갈린다.</summary>
    bool screenSpace = true;
};

/// <summary>
/// 텍스트 draw의 기저를 만들거나, 프레임이 그것을 기술할 수 없는 이유를 보고한다. 스크린 공간
/// 텍스트는 좌상단을 원점으로 렌더 타깃 픽셀로 측정되며 카메라를 결코 쓰지 않는다. 월드 공간
/// 텍스트는 자기 pixels-per-unit으로 배율되고 프레임의 카메라를 쓴다.
/// </summary>
/// <param name="backendName">거부를 보고하는 백엔드 이름이다. 예: "D3D12".</param>
[[nodiscard]] std::optional<TextQuadBasis> TryBuildTextQuadBasis(
    const RenderFrame& frame,
    const TextDraw& draw,
    const char* backendName);

/// <summary>
/// 글리프 하나를 기저 위에 놓는다. 글리프의 로컬 사각형은 텍스트 블록 중심 원점이다.
/// draw의 변환은 블록을 배치하고 이 함수는 그 안의 글리프를 배치한다.
/// </summary>
[[nodiscard]] QuadTransform PlaceTextGlyphQuad(
    const TextQuadBasis& basis,
    const TextDraw& draw,
    const TextGlyphQuad& glyph);


/// <summary>
/// 타일맵 draw의 공통 기저다: 프레임의 카메라 변환과 타일맵의 원점을 합친 것이며, 칸 하나하나가
/// 이 위에 놓인다. 프레임이 카메라를 싣지 않았으면 기술할 수 없다.
/// </summary>
/// <param name="backendName">거부를 보고하는 백엔드 이름이다. 예: "D3D11".</param>
[[nodiscard]] std::optional<Math::Matrix4x4> TryBuildTilemapBasis(
    const RenderFrame& frame,
    const TilemapDraw& draw,
    const char* backendName);

/// <summary>
/// 칸 하나를 기저 위에 놓는다. 칸의 자리는 격자 좌표와 칸 크기가 정하고, 보여 줄 그림은 칸이
/// 실어 온 UV 사각형이 정한다 — 스프라이트 한 장을 놓는 것과 같은 산술이다.
/// </summary>
[[nodiscard]] QuadTransform PlaceTilemapTileQuad(
    const Math::Matrix4x4& basis,
    const TilemapDraw& draw,
    const TilemapTile& tile);

}
