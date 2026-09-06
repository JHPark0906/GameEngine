#pragma once
#include <cstddef>
#include <memory>
#include <vector>

#include "../Math/Matrix.h"
#include "../Math/Vector.h"
#include "Component.h"

namespace GameEngine::Runtime
{

/// <summary>GameObject의 위치, 회전 및 크기 변환을 보관하는 필수 컴포넌트이다.</summary>
class Transform final : public Component
{
public:
    /// <summary>
    /// 부모의 자식 목록에서 자신을 빼고 자식들의 부모를 비운다. 정상 경로에서는 OnRemoved가
    /// 먼저 하지만, 어느 경로로 파괴되든 다른 Transform에 이 객체를 가리키는 포인터가 남지
    /// 않아야 한다.
    /// </summary>
    ~Transform() override;
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>로컬 위치를 반환한다.</summary>
    /// <returns>현재 로컬 위치이다.</returns>
    [[nodiscard]] const Math::Vector3& GetPosition() const { return mPosition; }

    /// <summary>로컬 위치를 변경한다.</summary>
    /// <param name="position">새 로컬 위치이다.</param>
    void SetPosition(const Math::Vector3& position)
    {
        mPosition = position;
        InvalidateWorldMatrix();
    }

    /// <summary>로컬 Euler 회전을 도 단위로 반환한다.</summary>
    /// <returns>X=pitch, Y=yaw, Z=roll인 도 단위 회전이다.</returns>
    [[nodiscard]] const Math::Vector3& GetRotation() const { return mRotation; }

    /// <summary>엔진 roll-pitch-yaw 규약을 사용하는 로컬 Euler 회전을 설정한다.</summary>
    /// <param name="rotation">X=pitch, Y=yaw, Z=roll인 도 단위 회전이다.</param>
    void SetRotation(const Math::Vector3& rotation)
    {
        mRotation = rotation;
        InvalidateWorldMatrix();
    }

    /// <summary>로컬 크기 배율을 반환한다.</summary>
    /// <returns>현재 로컬 크기 배율이다.</returns>
    [[nodiscard]] const Math::Vector3& GetScale() const { return mScale; }

    /// <summary>로컬 크기 배율을 변경한다.</summary>
    /// <param name="scale">새 로컬 크기 배율이다.</param>
    void SetScale(const Math::Vector3& scale)
    {
        mScale = scale;
        InvalidateWorldMatrix();
    }

    /// <summary>부모 Transform을 설정한다.</summary>
    /// <returns>순환 계층이 아니어서 설정에 성공하면 true이다.</returns>
    /// <summary>
    /// 부모를 바꾼다. 순환이나 장면 경계를 넘는 요청은 거절하고 false를 반환한다.
    /// </summary>
    /// <param name="parent">새 부모다. null이면 루트가 된다.</param>
    /// <param name="worldPositionStays">
    /// true면 월드에서의 위치·회전·크기를 유지하도록 로컬 값을 다시 계산한다 — 에디터에서
    /// 드래그해 옮길 때 객체가 그 자리에 머무는 방식이다. false면 로컬 값을 그대로 두어 새
    /// 부모 기준으로 놓인다 — 로더가 파일의 로컬 값을 그대로 세울 때의 방식이다.
    /// </param>
    bool SetParent(Transform* parent, bool worldPositionStays = false);

    /// <summary>
    /// 형제들 중 맨 뒤로 옮긴다. 부모가 없으면 아무것도 하지 않는다.
    ///
    /// UI에서 이것이 "맨 위로 올린다"이다. 그리는 것도 커서를 받는 것도 계층 순서를 따르고
    /// 뒤가 위이므로, 창을 앞으로 가져오는 일은 z 값을 따로 두는 것이 아니라 형제 순서를
    /// 바꾸는 것이다 — 순서가 둘이 되면 둘이 어긋날 수 있고, 그 어긋남이 곧 "보이는 것과
    /// 눌리는 것이 다르다"는 증상이 된다.
    /// </summary>
    void SetAsLastSibling();

    /// <summary>부모의 자식 목록에서 차지하는 위치다. 부모가 없으면 0이다.</summary>
    [[nodiscard]] std::size_t GetSiblingIndex() const;
    /// <summary>
    /// 부모의 자식 목록 안에서 옮긴다. 범위를 넘으면 마지막으로 옮기며, 부모가 없으면
    /// 아무것도 하지 않는다. 로컬 값과 월드 변환은 바뀌지 않는다.
    /// </summary>
    void SetSiblingIndex(std::size_t index);

    [[nodiscard]] Transform* GetParent() const { return mParent; }
    [[nodiscard]] const std::vector<Transform*>& GetChildren() const { return mChildren; }
    [[nodiscard]] Math::Matrix4x4 GetLocalToWorldMatrix() const;
    [[nodiscard]] Math::Vector3 GetWorldPosition() const;

    /// <summary>
    /// 부모가 있어도 월드 공간의 위치를 맞춘다. 부모 행렬을 되돌릴 수 없으면 로컬 위치를
    /// 추측해 쓰지 않고 false를 돌려준다.
    /// </summary>
    /// <param name="position">설정할 월드 공간 위치다.</param>
    /// <returns>새 위치를 로컬 공간으로 안전하게 바꿔 썼으면 true다.</returns>
    bool SetWorldPosition(const Math::Vector3& position);

private:
    [[nodiscard]] std::unique_ptr<Component> Clone() const override;
    void OnRemoved() override;
    [[nodiscard]] bool IsDescendantOf(const Transform* transform) const;
    /// <summary>로컬 변환 행렬을 위치·회전·크기로 분해해 이 Transform에 쓴다.</summary>
    void SetFromLocalMatrix(const Math::Matrix4x4& local);

    /// <summary>
    /// 자신과 자손의 월드 행렬 캐시를 무효화한다. 로컬 값이나 부모가 바뀌는 모든 길이 이것을
    /// 지나야 한다. 이미 dirty인 노드에서 멈춘다: dirty 노드의 자손은 언제나 dirty다 — 자손이
    /// 캐시를 다시 채우려면 부모 사슬부터 계산해 클린으로 만들기 때문이다.
    /// </summary>
    void InvalidateWorldMatrix();

    Math::Vector3 mPosition;
    Math::Vector3 mRotation;
    Math::Vector3 mScale{ 1.0f, 1.0f, 1.0f };
    Transform* mParent = nullptr;
    std::vector<Transform*> mChildren;

    // 프레임 조립이 렌더러마다 월드 행렬을 묻는다. 캐시가 없으면 그때마다 부모 사슬을 걸어
    // 다시 곱하게 되고, 깊은 계층에서는 그 비용이 조립 시간에 그대로 보인다.
    mutable Math::Matrix4x4 mCachedWorldMatrix;
    mutable bool mWorldMatrixDirty = true;
};

}
