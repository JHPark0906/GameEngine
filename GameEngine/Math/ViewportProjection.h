#pragma once

#include "Matrix.h"
#include "Vector.h"

namespace GameEngine::Math
{

/// <summary>월드 점을 뷰포트에 투영한 결과이다. 카메라 뒤의 점은 픽셀 좌표가 의미를 잃는다.</summary>
struct ViewportPoint
{
    /// <summary>뷰포트 좌상단 기준 픽셀 좌표이다. isInFront가 거짓이면 무의미하다.</summary>
    float x = 0.0f;
    float y = 0.0f;
    /// <summary>점이 카메라 앞(클립 공간 w &gt; 0)에 있는지 여부이다.</summary>
    // near/far나 뷰포트 안쪽 판정은 아니다. 직교 투영에서는 카메라 뒤의 점도 w가 양수일 수 있다.
    bool isInFront = false;
};

/// <summary>
/// 월드 점을 뷰포트 픽셀로 옮긴다. 원근 분할까지 마친 좌표이며, 원점은 좌상단이고 +y는
/// 아래다 — UI가 픽셀을 말하는 방식 그대로다.
/// </summary>
/// <param name="worldPoint">투영할 월드 좌표이다.</param>
/// <param name="viewProjection">뷰 행렬과 투영 행렬을 곱한 것이다.</param>
/// <param name="viewportWidth">뷰포트의 가로 픽셀 수이다.</param>
/// <param name="viewportHeight">뷰포트의 세로 픽셀 수이다.</param>
/// <returns>픽셀 좌표와, 그것이 쓸 만한지를 말하는 isInFront이다.</returns>
[[nodiscard]] ViewportPoint ProjectToViewport(
    const Vector3& worldPoint, const Matrix4x4& viewProjection,
    float viewportWidth, float viewportHeight);

/// <summary>
/// 커서가 화면에서 움직인 거리를 한 축 위의 월드 이동량으로 바꾼다.
///
/// 축의 월드 1단위가 화면에서 몇 픽셀인지를 먼저 구하고 — 축 원점과 원점+방향을 각각 투영해
/// 뺀 것이 그 벡터다 — 커서 이동을 그 벡터에 정사영한다. 그래서 카메라가 돌거나 멀어져도
/// 핸들이 커서를 따라간다. 축이 화면에서 거의 한 점으로 보이면(시선과 나란하면) 나눌 것이
/// 없으므로 0을 답한다: 그 자세에서는 어떤 픽셀 이동도 축 위의 거리를 뜻하지 못한다.
/// </summary>
/// <param name="axisOrigin">축이 시작하는 월드 좌표 — 끌고 있는 물체의 위치다.</param>
/// <param name="axisDirection">축의 단위 방향 벡터이다.</param>
/// <param name="viewProjection">뷰 행렬과 투영 행렬을 곱한 것이다.</param>
/// <param name="viewportWidth">뷰포트의 가로 픽셀 수이다.</param>
/// <param name="viewportHeight">뷰포트의 세로 픽셀 수이다.</param>
/// <param name="dragDeltaPixelsX">커서가 가로로 움직인 픽셀 수이다.</param>
/// <param name="dragDeltaPixelsY">커서가 세로로 움직인 픽셀 수이다. 아래가 양수다.</param>
/// <returns>축 방향으로 움직여야 할 월드 거리이다.</returns>
[[nodiscard]] float ComputeAxisDragDistance(
    const Vector3& axisOrigin, const Vector3& axisDirection, const Matrix4x4& viewProjection,
    float viewportWidth, float viewportHeight, float dragDeltaPixelsX, float dragDeltaPixelsY);

/// <summary>
/// 값을 격자에 맞춘다. 간격이 0 이하면 격자가 없다는 뜻이므로 값을 그대로 돌려준다.
/// </summary>
/// <param name="value">맞출 값이다.</param>
/// <param name="spacing">격자 간격이다.</param>
/// <returns>가장 가까운 격자점의 값이다.</returns>
[[nodiscard]] float SnapToGrid(float value, float spacing);

}
