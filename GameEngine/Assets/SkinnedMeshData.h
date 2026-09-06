#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "../Math/Aabb3D.h"

// A skinned mesh is imported straight into the layout the skinning shader declares, the same
// reason MeshData borrows from Core rather than Rendering.
#include "../Core/VertexLayout.h"

namespace GameEngine::Assets
{

/// <summary>
/// 프론트엔드가 한 draw를 위해 임포트한, 뼈 영향 정보를 가진 메시 형상이다.
///
/// <see cref="MeshData"/>와 별도 타입인 이유는 그것과 같다: 뼈 인덱스·가중치를 담는
/// <c>SkinnedMeshVertex</c>는 <c>MeshVertex</c>와 바이트 레이아웃이 다르고, 대부분의 메시는
/// 정적이라 이 자리를 쓰지 않는다. 정점 자체의 뼈 영향을 담을 뿐, 골격의 계층이나 애니메이션
/// 클립은 여기 없다 — 이 타입은 형상이고, 그것들은 별개의 계약이다.
/// </summary>
struct SkinnedMeshData
{
    /// <summary>
    /// 백엔드 GPU 캐시를 위한 정체성이다. <see cref="MeshData::id"/>와 같은 규칙이다: 재사용되지
    /// 않으므로 백엔드는 정점을 다시 들여다보지 않고 id에 대고 업로드를 캐시해도 된다.
    /// </summary>
    std::uint64_t id = 0;
    std::vector<Core::SkinnedMeshVertex> vertices;
    std::vector<std::uint32_t> indices;

    /// <summary>
    /// 바인드 포즈에서 이 형상을 감싸는 가장 작은 상자다. 스키닝은 포즈에 따라 실제 경계를
    /// 바꾸므로, 이 값은 대략의 컬링 기준일 뿐 애니메이션 중의 정확한 경계가 아니다.
    /// </summary>
    Math::Aabb3D bounds = Math::Aabb3D::Empty();

    [[nodiscard]] bool IsValid() const
    {
        return id != 0 && !vertices.empty() && !indices.empty();
    }

    [[nodiscard]] std::size_t GetByteSize() const
    {
        return vertices.size() * sizeof(Core::SkinnedMeshVertex) +
            indices.size() * sizeof(std::uint32_t);
    }
};

/// <summary>
/// 그 정점들을 감싸는 가장 작은 상자다. <see cref="ComputeBounds"/>(MeshData.h)와 같은 이유로
/// 이름 붙은 함수로 서 있다 — 파일도 창도 없이 시험할 수 있게.
/// </summary>
[[nodiscard]] inline Math::Aabb3D ComputeSkinnedBounds(
    const std::span<const Core::SkinnedMeshVertex> vertices)
{
    Math::Aabb3D bounds = Math::Aabb3D::Empty();
    for (const Core::SkinnedMeshVertex& vertex : vertices)
    {
        bounds.Encapsulate({ vertex.position.x, vertex.position.y, vertex.position.z });
    }
    return bounds;
}

}
