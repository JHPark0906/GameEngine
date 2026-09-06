#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace GameEngine::Core
{

/// <summary>
/// 정점과 셰이더 상수가 공유하는 평범한 실수 묶음이다. 그래픽스 API의 벡터 타입이 아니라 plain
/// float라서 어느 API 계열의 백엔드도 이것을 그대로 쓰고, 에셋 계층은 렌더링을 모른 채 정점을
/// 담는다.
/// </summary>
struct Float2
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Float3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Float4
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

/// <summary>행 우선 4x4 행렬이다. 상수 버퍼가 받는 그대로의 형태다.</summary>
struct Float4x4
{
    std::array<float, 16> elements{};
};

/// <summary>mesh 프로그램의 VS_INPUT과 일치한다.</summary>
struct MeshVertex
{
    Float3 position;
    Float3 normal;
    Float2 textureCoordinate;
};

/// <summary>sprite와 text 프로그램의 VS_INPUT과 일치한다.</summary>
struct SpriteVertex
{
    Float2 position;
    Float2 textureCoordinate;
};

/// <summary>
/// skinned mesh 프로그램의 VS_INPUT과 일치한다. 골격 없는 <see cref="MeshVertex"/>에 뼈 영향
/// 정보를 더한 것이며, 별도 구조체인 이유는 대부분의 메시가 정적이라 이 자리를 쓰지 않기
/// 때문이다 — 모든 정점에 강제로 얹으면 안 쓰는 32바이트가 매 정점마다 남는다.
///
/// 뼈 인덱스는 골격의 뼈 배열을 가리키는 정수 넷이고, 가중치는 그 넷에 대응하는 영향력이다.
/// 넷은 이 엔진이 임의로 고른 상한이 아니라 glTF·Unity·Unreal이 공유하는 업계 관례다.
/// </summary>
struct SkinnedMeshVertex
{
    Float3 position;
    Float3 normal;
    Float2 textureCoordinate;
    std::array<std::uint32_t, 4> boneIndices{};
    Float4 boneWeights;
};

}
