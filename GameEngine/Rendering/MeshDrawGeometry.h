#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "../Math/Color.h"
#include "../Math/Matrix.h"
#include "RenderFrame.h"
#include "ShaderInterop.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 메시 하나가 어디에 놓이고 무엇에 비춰지는지이다. 엔진의 표준 클립 공간 기준이다.
///
/// QuadTransform의 메시 짝이다: 메시를 그리는 데 모든 백엔드에 필요한 것이 정확히 이 두 행렬과
/// 프레임의 조명이고, 어느 것도 그래픽 API에 의존하지 않는다. 백엔드마다가 아니라 여기서
/// 계산하는 것이 두 백엔드가 같은 메시를 다르게 놓거나 다르게 비추는 일을 막는다.
/// </summary>
struct MeshShading
{
    Math::Matrix4x4 worldViewProjection;
    Math::Matrix4x4 world;
    Math::Matrix4x4 normalToWorld;
    Math::Color ambientLight = Math::Color::Black;
    std::array<LightRenderData, MaxFrameLights> lights;
    std::size_t lightCount = 0;
    /// <summary><see cref="MeshDraw::tint"/>가 여기로 옮겨 온다. 기본값은 흰색이다.</summary>
    Math::Color tint = Math::Color::White;
    /// <summary>공통 메시 패스가 정한 블렌딩 모드다. 참이면 깊이 검사는 유지하고 쓰기는 끈다.</summary>
    bool alphaBlended = false;
};

/// <summary>
/// 조명과 재질 색을 셰이더 상수의 모양으로 옮긴다. 행렬은 백엔드가 자기 규약으로 저장하지만,
/// 광원을 어느 칸에 어떤 부호로 넣는지와 틴트를 그대로 옮기는 것은 셰이더가 정한 모양이라 한
/// 곳에서만 한다.
/// </summary>
void StoreMeshLighting(const MeshShading& shading, MeshConstants& constants);

/// <summary>
/// 로컬-월드 변환 하나로 배치를 만들거나, 프레임이 그것을 기술할 수 없는 이유를 보고한다. 메시는
/// 언제나 프레임의 카메라를 쓰고 프론트엔드가 그것을 보장하므로, 카메라가 없다는 것은 백엔드가
/// 자기 투영으로 덮어 가릴 일이 아니라 계약 위반이다.
///
/// <see cref="MeshDraw"/>와 <see cref="SkinnedMeshDraw"/> 둘 다 이 변환 하나만 배치에 쓰므로,
/// draw 형식이 아니라 이 값 하나를 받는다 — 새 payload가 늘 때마다 오버로드가 늘지 않는다.
/// </summary>
/// <param name="backendName">거부를 보고하는 백엔드 이름이다. 예: "D3D11".</param>
[[nodiscard]] std::optional<MeshShading> TryBuildMeshShading(
    const RenderFrame& frame,
    const Math::Matrix4x4& localToWorld,
    const char* backendName);

/// <summary><see cref="MeshDraw"/>의 <c>localToWorld</c>로 배치를 만든다.</summary>
[[nodiscard]] inline std::optional<MeshShading> TryBuildMeshShading(
    const RenderFrame& frame,
    const MeshDraw& draw,
    const char* const backendName)
{
    return TryBuildMeshShading(frame, draw.localToWorld, backendName);
}

}
