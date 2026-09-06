#pragma once

#include <array>
#include <cstdint>

#include "../Math/Float.h"

namespace GameEngine::Assets
{

/// <summary>mesh 프로그램의 VS_INPUT과 일치한다.</summary>
struct MeshVertex
{
    Math::Float3 position;
    Math::Float3 normal;
    Math::Float2 textureCoordinate;
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
    Math::Float3 position;
    Math::Float3 normal;
    Math::Float2 textureCoordinate;
    std::array<std::uint32_t, 4> boneIndices{};
    Math::Float4 boneWeights;
};

}
