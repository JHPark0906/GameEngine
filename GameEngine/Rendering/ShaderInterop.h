#pragma once

#include <array>
#include <cstddef>

#include "../Core/VertexLayout.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 엔진이 셰이더와 공유하는 모든 레이아웃의 단 하나뿐인 C++ 정의이다.
///
/// 그래픽 API의 벡터 타입이 아니라 일부러 순수 float이다. 한 API의 타입으로 적으면 다른 API
/// 계열의 백엔드는 전부 다시 정의해야 하고, 하나의 정의가 Direct3D 11과 12 사이에서 막아 주는
/// 바로 그 어긋남을 API 계열 사이에 다시 들여오게 된다. 백엔드는 GPU로 가는 길에 이것을 자기
/// 수학 라이브러리가 원하는 형태로 변환한다.
///
/// 모든 멤버를 셰이딩 언어 쪽의 대응 선언과 동기화해서 유지하라.
/// </summary>

// 정점 레이아웃과 실수 묶음은 Core의 것이다: 에셋이 정점을 담으려면 렌더링 아래에 있어야
// 한다. 셰이더 쪽 이름은 그 타입들을 가리키는 별칭이다.
using ShaderFloat2 = Core::Float2;
using ShaderFloat3 = Core::Float3;
using ShaderFloat4 = Core::Float4;
using ShaderFloat4x4 = Core::Float4x4;
using MeshVertex = Core::MeshVertex;
using SpriteVertex = Core::SpriteVertex;
using SkinnedMeshVertex = Core::SkinnedMeshVertex;

/// <summary>
/// 광원 하나가 셰이더에 놓이는 모양이다. 방향광은 xyz가 빛이 나아가는 방향이고 w가 0, 점광은
/// xyz가 위치이고 w가 1이다. 색의 w는 점광의 range다.
/// </summary>
struct ShaderLight
{
    ShaderFloat4 positionOrDirection{ 0.0f, 0.0f, 1.0f, 0.0f };
    ShaderFloat4 colorAndRange{ 0.0f, 0.0f, 0.0f, 1.0f };
};

/// <summary>
/// 셰이더가 받는 광원 수의 상한이다. RenderFrame의 MaxFrameLights와 같아야 한다 — 프레임이
/// 실을 수 있는 것과 셰이더가 읽을 수 있는 것이 다르면 어느 한쪽이 조용히 잘린다.
/// </summary>
inline constexpr std::size_t ShaderMaxLights = 3;

/// <summary>
/// mesh 프로그램의 상수 버퍼와 일치한다. 조명은 프레임이 실어 온 그대로이며 기본값은 빛이
/// 없다. <c>tint</c>는 알베도 텍스처에 곱해질 재질 색이며, 기본값은 흰색 — 곱해도 텍스처를
/// 바꾸지 않는 값 — 이다.
/// </summary>
struct MeshConstants
{
    ShaderFloat4x4 worldViewProjection;
    ShaderFloat4x4 world;
    ShaderFloat4x4 normalToWorld;
    ShaderFloat4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };
    /// <summary>rgb는 주변광, w는 유효한 광원 수다.</summary>
    ShaderFloat4 ambientAndLightCount{ 0.0f, 0.0f, 0.0f, 0.0f };
    ShaderLight lights[ShaderMaxLights];
};

/// <summary>
/// 한 골격이 실을 수 있는 뼈 수의 상한이다. skinned mesh 프로그램의 두 번째 상수 버퍼가 이
/// 크기의 배열을 선언하므로, 골격이 이보다 많은 뼈를 가지면 임포트가 거부한다.
///
/// 128 * 64바이트(행렬 하나) = 8192바이트로, Direct3D 11의 상수 버퍼 상한인
/// 65536바이트 안에 들어간다. 따라서 구조화 버퍼 바인딩 없이 두 번째 cbuffer로 전달한다.
/// </summary>
inline constexpr std::size_t MaxSkinnedMeshBones = 128;

/// <summary>skinned mesh 프로그램의 두 번째 상수 버퍼와 일치한다. 뼈마다 모델 공간 스키닝 행렬 하나다.</summary>
struct BoneConstants
{
    ShaderFloat4x4 boneMatrices[MaxSkinnedMeshBones];
};

/// <summary>sprite 프로그램의 상수 버퍼와 일치한다.</summary>
struct SpriteConstants
{
    ShaderFloat4x4 worldViewProjection;
    ShaderFloat4 tint;
    ShaderFloat4 uvTransform;
};

/// <summary>text 프로그램의 상수 버퍼와 일치한다.</summary>
struct TextConstants
{
    ShaderFloat4x4 worldViewProjection;
    ShaderFloat4 tint;
    /// <summary>xy는 UV 배율, zw는 오프셋이다. 글리프의 아틀라스 사각형이 여기 실린다.</summary>
    ShaderFloat4 uvTransform;
};

// HLSL packs a constant buffer into 16-byte rows, so every constant layout must be a multiple of 16
// and each member must begin on a 16-byte boundary. These checks fail the build if a member is added
// or reordered in a way the shader side would read differently. A shading language with different
// packing rules asserts its own expectations where it declares its pipelines; these hold for the
// layouts as written, which is what every backend uploads.
static_assert(sizeof(MeshVertex) == 32, "MeshVertex must stay tightly packed for its input layout.");
static_assert(sizeof(SpriteVertex) == 16, "SpriteVertex must stay tightly packed for its input layout.");
static_assert(
    sizeof(SkinnedMeshVertex) == 64,
    "SkinnedMeshVertex must stay tightly packed for its input layout.");
static_assert(sizeof(ShaderFloat4x4) == 64, "A shader matrix must be sixteen tightly packed floats.");
static_assert(sizeof(ShaderLight) == 32);
static_assert(sizeof(MeshConstants) == 320 && sizeof(MeshConstants) % 16 == 0);
static_assert(sizeof(BoneConstants) == MaxSkinnedMeshBones * 64 && sizeof(BoneConstants) % 16 == 0);
static_assert(sizeof(SpriteConstants) == 96 && sizeof(SpriteConstants) % 16 == 0);
static_assert(sizeof(TextConstants) == 96 && sizeof(TextConstants) % 16 == 0);
static_assert(offsetof(MeshConstants, world) == 64 && offsetof(MeshConstants, normalToWorld) == 128 &&
    offsetof(MeshConstants, tint) == 192 && offsetof(MeshConstants, ambientAndLightCount) == 208 &&
    offsetof(MeshConstants, lights) == 224);
static_assert(offsetof(SpriteConstants, tint) == 64 && offsetof(SpriteConstants, uvTransform) == 80);
static_assert(offsetof(TextConstants, tint) == 64 && offsetof(TextConstants, uvTransform) == 80);
static_assert(offsetof(MeshVertex, normal) == 12 && offsetof(MeshVertex, textureCoordinate) == 24);
static_assert(offsetof(SpriteVertex, textureCoordinate) == 8);
static_assert(
    offsetof(SkinnedMeshVertex, normal) == 12 && offsetof(SkinnedMeshVertex, textureCoordinate) == 24 &&
    offsetof(SkinnedMeshVertex, boneIndices) == 32 && offsetof(SkinnedMeshVertex, boneWeights) == 48);

/// <summary>
/// 원점 중심의 단위 quad이다. 시계 방향으로 감기며, sprite와 text 프로그램이 함께 쓴다.
/// 데이터를 공유하면 두 백엔드가 같은 quad를 다른 winding으로 그리는 일이 없다.
/// </summary>
inline constexpr std::array<SpriteVertex, 6> SpriteQuadVertices{
    SpriteVertex{ { -0.5f,  0.5f }, { 0.0f, 0.0f } },
    SpriteVertex{ {  0.5f,  0.5f }, { 1.0f, 0.0f } },
    SpriteVertex{ {  0.5f, -0.5f }, { 1.0f, 1.0f } },
    SpriteVertex{ { -0.5f,  0.5f }, { 0.0f, 0.0f } },
    SpriteVertex{ {  0.5f, -0.5f }, { 1.0f, 1.0f } },
    SpriteVertex{ { -0.5f, -0.5f }, { 0.0f, 1.0f } },
};

}
