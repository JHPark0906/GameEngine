#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "../Math/Aabb3D.h"

// The engine has one vertex layout and the shaders declare it, so a mesh is imported straight into
// it. That is the only thing this borrows from Rendering, and it is a leaf header.
#include "../Core/VertexLayout.h"

namespace GameEngine::Assets
{

/// <summary>
/// 프론트엔드가 한 draw를 위해 임포트한 메시 형상이다. 모든 백엔드가 업로드하는 정점 레이아웃
/// 그대로다.
///
/// 프레임이 파일 경로를 실어 각 백엔드가 직접 파싱하게 하면, 새 그래픽 API마다 모델을 그리기
/// 전에 임포트부터 다시 구현해야 하고 두 백엔드가 파일 내용에 대해 서로 다르게 굴 여지가
/// 생긴다. 프레임은 완성 데이터를 나르므로, 정점을 나른다.
///
/// draw들은 shared_ptr로 형상을 공유하므로 모델 하나는 임포트 한 번 비용이고, 백엔드가 끝나기
/// 전에 프론트엔드 캐시가 항목을 퇴거해도 프레임은 유효하게 남는다.
/// </summary>
struct MeshData
{
    /// <summary>
    /// 백엔드 GPU 캐시를 위한 정체성이다. 하나의 id가 다른 형상에 재사용되는 일은 없고, 같은
    /// 파일은 프론트엔드가 데이터를 캐시하지 못한 프레임을 지나서도 자기 id를 유지하므로,
    /// 백엔드는 정점을 다시 들여다보지 않고 id에 대고 업로드를 캐시해도 된다.
    /// </summary>
    std::uint64_t id = 0;
    std::vector<Core::MeshVertex> vertices;
    std::vector<std::uint32_t> indices;

    /// <summary>
    /// 로컬 공간에서 이 형상을 감싸는 가장 작은 상자다. 정점을 읽은 그 자리에서 한 번
    /// 계산되고, 그 뒤로는 다시 정점을 훑지 않는다.
    ///
    /// 컬링이 매 프레임 「이 메시가 카메라 안에 있는가」를 물으려면 상자가 있어야 하는데,
    /// 물을 때마다 정점을 훑으면 컬링이 아끼려던 것보다 비싸진다. 그래서 형상과 함께 산다.
    ///
    /// <b>메모리에만 있고 파일에 적히지 않는다.</b> 정점에서 결정되는 값이라 따로 적으면
    /// 형상과 어긋날 수 있는 두 번째 진실이 생긴다 — 에셋 파일이 바뀌었는데 적어 둔 상자가
    /// 옛것이면, 틀린 상자로 거른 오브젝트가 오류도 로그도 없이 화면에서 사라진다.
    /// 다시 읽을 때 다시 계산하는 편이 언제나 맞다.
    /// </summary>
    Math::Aabb3D bounds = Math::Aabb3D::Empty();

    [[nodiscard]] bool IsValid() const
    {
        return id != 0 && !vertices.empty() && !indices.empty();
    }

    [[nodiscard]] std::size_t GetByteSize() const
    {
        return vertices.size() * sizeof(Core::MeshVertex) + indices.size() * sizeof(std::uint32_t);
    }
};

/// <summary>
/// 그 정점들을 감싸는 가장 작은 상자다. 정점이 없으면 빈 상자를 돌려준다.
///
/// 임포터 안이 아니라 이름 붙은 함수로 서 있는 이유는 <b>시험할 수 있게 하기 위해서</b>다.
/// 상자가 조용히 한 축만 틀리면 그 메시는 어떤 각도에서 사라지고, 그것은 오류도 로그도 남기지
/// 않는 종류의 잘못이다. 여기서는 정점 몇 개와 기대하는 두 모서리만 있으면 되며, 파일도 창도
/// 필요하지 않다.
/// </summary>
/// <param name="vertices">로컬 공간의 정점들이다.</param>
[[nodiscard]] inline Math::Aabb3D ComputeBounds(const std::span<const Core::MeshVertex> vertices)
{
    Math::Aabb3D bounds = Math::Aabb3D::Empty();
    for (const Core::MeshVertex& vertex : vertices)
    {
        bounds.Encapsulate({ vertex.position.x, vertex.position.y, vertex.position.z });
    }
    return bounds;
}

}
