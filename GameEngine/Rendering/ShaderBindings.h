#pragma once

#include "RenderFrame.h"
#include "ShaderProgram.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 셰이더 프로그램이 상수, 텍스처, 샘플러를 기대하는 슬롯이다.
///
/// 슬롯은 리소스 종류별로 번호가 매겨지는데, 이는 이 엔진이 겨냥하는 모든 그래픽 API가 바인딩을
/// 기술하는 방식이다 — Direct3D는 `b0`, `t0`, `s1`로 쓰고, Vulkan과 Metal도 자기 바인딩 지점에
/// 같은 방식으로 번호를 붙인다. 백엔드는 이것을 자기 모델에 대응시킨다: Direct3D 11은 그대로
/// 컨텍스트에 넘기고, Direct3D 12는 이것으로 루트 시그니처를 만들며, 다른 방식의 API는 매 호출
/// 지점에서 다시 알아내는 대신 한 곳에서 번역한다.
/// </summary>
struct ShaderBindingSlots
{
    unsigned int constantBuffer = 0;
    unsigned int texture = 0;
    unsigned int sampler = 0;
};

/// <summary>
/// 셰이더 프로그램이 구현하는 파이프라인이다. 두 열거는 일부러 분리되어 있다 — 하나는 프레임
/// 계약에 속해 draw가 무엇을 원하는지 말하고, 다른 하나는 빌드에 속해 실행 파일 옆에 무엇이
/// 있어야 하는지 말한다 — 그러나 백엔드는 `PipelineKind`를 쥐고 그에 대해 물으려면 프로그램이
/// 필요하므로, 그 대응을 호출 지점마다 가정하는 대신 여기에 적어 둔다.
/// </summary>
[[nodiscard]] constexpr ShaderProgram GetShaderProgram(const PipelineKind kind)
{
    switch (kind)
    {
    case PipelineKind::Mesh: return ShaderProgram::Mesh;
    case PipelineKind::Sprite: return ShaderProgram::Sprite;
    case PipelineKind::Text: return ShaderProgram::Text;
    case PipelineKind::SkinnedMesh: return ShaderProgram::SkinnedMesh;
    }
    return ShaderProgram::Mesh;
}

/// <summary>
/// 프로그램이 바인딩에 사용하는 슬롯이다.
///
/// 이것이 셰이더 인터페이스다. 숫자는 여기 한 번 선언되고, 백엔드가 읽어 가며, HLSL은
/// 컴파일러의 전처리 정의로 받는다 — 그래서 엔진과 어긋난 셰이더는 잘못된 슬롯을 샘플링하는
/// 대신 컴파일에 실패한다. HLSL의 `register(b0)`와 바인딩 호출의 `0`을 각각 리터럴로 적으면
/// 아무것도 그 둘을 검사하지 않는다.
///
/// 메시와 quad 프로그램의 샘플러 슬롯이 일부러 다르다: 공유 루트 시그니처 하나에 샘플러를
/// 선언하는 백엔드는 다른 샘플러를 바인딩하는 것이 아니라 다른 슬롯으로 샘플링해서 주소 모드를
/// 고른다.
/// </summary>
[[nodiscard]] constexpr ShaderBindingSlots GetShaderBindings(const ShaderProgram program)
{
    // Each program binds one constant buffer and one texture, so those are slot zero everywhere.
    // Only the sampler distinguishes them, because only the address mode differs.
    switch (program)
    {
    case ShaderProgram::Mesh: return { 0, 0, 0 };
    case ShaderProgram::Sprite: return { 0, 0, 1 };
    case ShaderProgram::Text: return { 0, 0, 1 };
    // Mesh와 같은 슬롯이다: 완전히 다른 컴파일 단위(SkinnedMesh.hlsl)라서 겹칠 일이 없다.
    case ShaderProgram::SkinnedMesh: return { 0, 0, 0 };
    }
    return {};
}

/// <summary>
/// skinned mesh 프로그램이 뼈 행렬 배열에 쓰는 <b>두 번째</b> 상수 버퍼 슬롯이다. 다른 프로그램은
/// 상수 버퍼가 하나뿐이라 이 값을 <see cref="ShaderBindingSlots"/>에 얹지 않고 따로 둔다 — 그
/// 하나뿐인 필드를 이 값 하나만을 위해 배열이나 선택형으로 넓히는 것은, 쓰는 프로그램이 하나뿐인
/// 자리에 어울리지 않는다.
/// </summary>
inline constexpr unsigned int SkinnedMeshBoneConstantBufferSlot = 1;

/// <summary>이 파이프라인 종류 뒤의 프로그램이 바인딩에 사용하는 슬롯이다.</summary>
[[nodiscard]] constexpr ShaderBindingSlots GetShaderBindings(const PipelineKind kind)
{
    return GetShaderBindings(GetShaderProgram(kind));
}

static_assert(
    GetShaderBindings(ShaderProgram::Mesh).sampler !=
        GetShaderBindings(ShaderProgram::Sprite).sampler,
    "Two address modes cannot share one sampler slot: a backend that declares its samplers on a "
    "shared root signature selects the mode by slot.");
static_assert(
    GetShaderBindings(ShaderProgram::Sprite).sampler ==
        GetShaderBindings(ShaderProgram::Text).sampler,
    "The quad pipelines sample the same way, so they share one sampler declaration.");

}
