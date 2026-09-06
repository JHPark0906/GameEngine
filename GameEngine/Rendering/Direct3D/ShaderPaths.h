#pragma once

#include <filesystem>
#include <vector>

#include "../ShaderProgram.h"

namespace GameEngine::Rendering::Direct3D
{

/// <summary>
/// Direct3D 계열에서 셰이더 프로그램을 구현하는 HLSL 파일이다.
///
/// 프로그램을 파일로 바꾸는 곳은 여기뿐이고, 소스는 공유 `Rendering/Shaders`가 아니라 이 옆의
/// `Rendering/Direct3D/Shaders` 아래 있다. HLSL은 "엔진의 API 독립적 셰이더"가 아니라 Direct3D가
/// 컴파일하는 것이고, SPIR-V나 Metal 셰이딩 언어를 원하는 API는 자기 백엔드 옆에 자기 세트를
/// 가져온다. 백엔드는 "Mesh.hlsl"이 아니라 `ShaderProgram::Mesh`를 요구한다.
/// </summary>
[[nodiscard]] std::filesystem::path GetShaderRelativePath(ShaderProgram program);

/// <summary>
/// Direct3D 백엔드가 실행 파일 옆에서 찾아야 하는 모든 셰이더 아티팩트이다. 배포는 어떤 그래픽
/// API에 대해서든 전체 세트를 스테이징해야 한다: 그중 하나라도 컴파일하지 못하는 백엔드는
/// 초기화에 실패하고, `Auto`는 빌드 시점이 아니라 대상 머신에서 결정된다.
/// </summary>
[[nodiscard]] std::vector<std::filesystem::path> GetShaderRelativePaths();

/// <summary>
/// 프로그램의 한 진입점을 빌드 시 컴파일한 아티팩트가 배포되는 경로이다. 예:
/// `Rendering/Direct3D/Shaders/Mesh.VS.cso`. 런타임은 이것이 있으면 소스 컴파일 대신 로드한다.
/// </summary>
[[nodiscard]] std::filesystem::path GetCompiledShaderRelativePath(
    ShaderProgram program, const char* entryPoint);

}
