#pragma once

#include <array>
#include <cstddef>

#include "Vector.h"

namespace GameEngine::Math
{

struct Quaternion;

/// <summary>
/// 행 우선 저장, 행 벡터 규약의 4x4 행렬이다: 점은 `v * M`으로 변환되고, `A * B`는 A를 먼저
/// 적용한 뒤 B를 적용하는 변환이다. 위 세 행이 기저 벡터의 상, 넷째 행이 이동이다. 왼손
/// 좌표계이며 오일러 각은 roll(Z)·pitch(X)·yaw(Y) 순으로 적용된다.
/// </summary>
struct [[nodiscard]] Matrix4x4
{
    Matrix4x4() = default;

    /// <summary>기저 벡터 세 행과 이동으로 행렬을 만든다. 넷째 열은 (0, 0, 0, 1)이다.</summary>
    [[nodiscard]] static Matrix4x4 FromRows(
        const Vector3& row0, const Vector3& row1, const Vector3& row2, const Vector3& translation);

    /// <summary>쿼터니언의 회전 행렬이다.</summary>
    [[nodiscard]] static Matrix4x4 CreateRotation(const Quaternion& rotation);

    /// <summary>
    /// 배율 → 회전(오일러, 도) → 이동 순의 변환이다. Transform의 로컬 행렬이 이것이며, 배율과
    /// 회전과 이동을 따로 곱하는 것과 같다.
    /// </summary>
    [[nodiscard]] static Matrix4x4 CreateTransform(
        const Vector3& translation, const Vector3& rotationDegrees, const Vector3& scale);

    /// <summary>
    /// 배율·회전(오일러, 도)·이동으로 분해한다. CreateTransform의 역이다.
    /// 반사는 X 배율의 부호에 보존한다. 축별 부호 분해는 유일하지 않으며 짐벌 락에서는 roll이 0이다.
    /// </summary>
    void Decompose(Vector3& scale, Vector3& rotationDegrees, Vector3& translation) const;

    [[nodiscard]] Vector3 GetTranslation() const;

    [[nodiscard]] static Matrix4x4 Identity();
    [[nodiscard]] static Matrix4x4 CreateScale(const Vector3& scale);
    [[nodiscard]] static Matrix4x4 CreateTranslation(const Vector3& translation);
    [[nodiscard]] static Matrix4x4 CreateRotationRollPitchYawDegrees(const Vector3& rotation);
    /// <summary>
    /// 단일 축 회전이다. 도 단위다. 에셋 포맷은 자기만의 축 순서로 오일러 각을 지정하는데, 고정된
    /// roll-pitch-yaw 조합으로는 표현할 수 없으므로 임포터가 이것들을 직접 조합한다.
    /// </summary>
    [[nodiscard]] static Matrix4x4 CreateRotationXDegrees(float degrees);
    [[nodiscard]] static Matrix4x4 CreateRotationYDegrees(float degrees);
    [[nodiscard]] static Matrix4x4 CreateRotationZDegrees(float degrees);
    /// <summary>
    /// 엔진의 표준 클립 공간으로의 투영이다: 왼손 좌표계, +Y 위쪽, 깊이 [0, 1].
    ///
    /// 이것은 호출자가 카메라마다 하는 선택이 아니라 엔진의 규약이다. Runtime과 렌더링
    /// 프론트엔드는 언제나 이 공간의 행렬을 만들고, 다른 손 방향이나 깊이 범위를 기대하는 API의
    /// 그래픽 백엔드는 GPU로 가는 길에 변환한다.
    /// </summary>
    [[nodiscard]] static Matrix4x4 CreatePerspectiveFieldOfViewLeftHanded(
        float fieldOfViewDegrees, float aspectRatio, float nearClipPlane, float farClipPlane);
    /// <summary>같은 표준 클립 공간으로의 직교 투영이다.</summary>
    [[nodiscard]] static Matrix4x4 CreateOrthographicLeftHanded(
        float width, float height, float nearClipPlane, float farClipPlane);
    /// <summary>
    /// 독립적인 경계를 갖는, 같은 표준 클립 공간으로의 직교 투영이다. 스크린 공간은 아래 경계를
    /// 위 경계보다 위에 두고 쓰는데, 그것이 원점을 렌더 타깃 좌상단에 놓는다.
    /// </summary>
    [[nodiscard]] static Matrix4x4 CreateOrthographicOffCenterLeftHanded(
        float left, float right, float bottom, float top, float nearClipPlane, float farClipPlane);

    [[nodiscard]] float GetElement(std::size_t row, std::size_t column) const;
    [[nodiscard]] bool IsFinite() const;
    [[nodiscard]] Matrix4x4 operator*(const Matrix4x4& other) const;
    [[nodiscard]] Vector3 TransformPoint(const Vector3& point) const;
    /// <summary>
    /// 이동은 무시하고 회전·배율 부분만으로 방향을 변환한다. 법선이 이렇게 — 행렬 자체가 아니라
    /// 역전치를 통해 — 변환된다.
    /// </summary>
    [[nodiscard]] Vector3 TransformDirection(const Vector3& direction) const;
    [[nodiscard]] Matrix4x4 Transpose() const;
    /// <summary>행렬식이 음수면 변환이 뒤집는다는 뜻이고, 그것은 삼각형 winding을 뒤집는다.</summary>
    [[nodiscard]] float GetDeterminant() const;
    [[nodiscard]] bool TryInvert(Matrix4x4& inverse) const;

private:
    explicit Matrix4x4(const std::array<float, 16>& elements) : mElements(elements) {}

    std::array<float, 16> mElements{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

}
