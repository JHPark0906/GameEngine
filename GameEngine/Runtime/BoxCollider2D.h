#pragma once

#include "../Math/Vector.h"
#include "Collider2D.h"

namespace GameEngine::Runtime
{

/// <summary>
/// 사각형 콜라이더다. 오브젝트의 자리에서 <see cref="GetOffset"/>만큼 옮긴 곳을 가운데로 하고
/// <see cref="GetSize"/>만큼의 크기를 갖는다. 둘 다 오브젝트 로컬 단위라, 오브젝트가 커지면
/// 콜라이더도 함께 커진다.
/// </summary>
class BoxCollider2D final : public Collider2D
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>오브젝트의 자리에서 사각형 가운데까지의 로컬 거리다.</summary>
    [[nodiscard]] const Math::Vector2& GetOffset() const { return mOffset; }
    void SetOffset(const Math::Vector2& offset) { mOffset = offset; }

    /// <summary>사각형의 로컬 가로세로 길이다. 어느 쪽이든 0 이하면 이 콜라이더는 겹치지 않는다.</summary>
    [[nodiscard]] const Math::Vector2& GetSize() const { return mSize; }
    void SetSize(const Math::Vector2& size) { mSize = size; }

    [[nodiscard]] Core::Aabb2D GetWorldBounds() const override;

private:
    Math::Vector2 mOffset{ 0.0f, 0.0f };
    Math::Vector2 mSize{ 1.0f, 1.0f };
};

}
