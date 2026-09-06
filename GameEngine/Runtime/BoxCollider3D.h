#pragma once

#include "../Math/Vector.h"
#include "Collider3D.h"

namespace GameEngine::Runtime
{

/// <summary>
/// 상자 콜라이더다. 오브젝트의 자리에서 <see cref="GetOffset"/>만큼 옮긴 곳을 가운데로 하고
/// <see cref="GetSize"/>만큼의 로컬 크기를 갖는다.
/// </summary>
class BoxCollider3D final : public Collider3D
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>오브젝트의 자리에서 상자 가운데까지의 로컬 거리다.</summary>
    [[nodiscard]] const Math::Vector3& GetOffset() const { return mOffset; }
    void SetOffset(const Math::Vector3& offset) { mOffset = offset; }

    /// <summary>상자의 로컬 가로·세로·깊이다. 어느 축이든 0 이하면 물리에는 참여하지 않는다.</summary>
    [[nodiscard]] const Math::Vector3& GetSize() const { return mSize; }
    void SetSize(const Math::Vector3& size) { mSize = size; }

    [[nodiscard]] Math::Aabb3D GetWorldBounds() const override;

private:
    Math::Vector3 mOffset{ 0.0f, 0.0f, 0.0f };
    Math::Vector3 mSize{ 1.0f, 1.0f, 1.0f };
};

}
