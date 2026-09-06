#pragma once

#include <array>
#include <string_view>

namespace GameEngine::Rendering
{

/// <summary>
/// 엔진이 그리는 데 쓰는 셰이더 프로그램이다. 구현 파일이 아니라 하는 일로 이름 붙인다.
///
/// 배포와 파이프라인 생성은 파일 경로가 아니라 프로그램을 요구하고, 각 백엔드는 자기 API에
/// 필요한 것으로 답한다. 셰이더를 HLSL 경로로만 알면 다른 언어를 다른 아티팩트로 컴파일하는
/// API의 백엔드는 대응시킬 이름이 없다.
///
/// 이것은 일부러 PipelineKind가 아니다. PipelineKind는 프레임 계약에 속해 draw가 무엇을 원하는지
/// 말하고, 이것은 빌드에 속해 실행 파일 옆에 무엇이 있어야 하는지 말한다. 오늘은 일대일이지만,
/// 그것을 표현하려고 배포를 프레임 계약에 묶는 것은 잘못된 의존이다.
/// </summary>
enum class ShaderProgram : unsigned char
{
    Mesh,
    Sprite,
    Text,
    SkinnedMesh,
};

/// <summary>백엔드가 제공할 수 있어야 하는 모든 프로그램이다. 배포가 열거한다.</summary>
inline constexpr std::array<ShaderProgram, 4> AllShaderPrograms{
    ShaderProgram::Mesh, ShaderProgram::Sprite, ShaderProgram::Text, ShaderProgram::SkinnedMesh };

/// <summary>진단용 프로그램 이름이다.</summary>
[[nodiscard]] constexpr std::string_view GetShaderProgramName(const ShaderProgram program)
{
    switch (program)
    {
    case ShaderProgram::Mesh: return "Mesh";
    case ShaderProgram::Sprite: return "Sprite";
    case ShaderProgram::Text: return "Text";
    case ShaderProgram::SkinnedMesh: return "SkinnedMesh";
    }
    return "Unknown";
}

}
