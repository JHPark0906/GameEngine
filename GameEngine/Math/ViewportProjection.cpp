#include "pch.h"
#include "ViewportProjection.h"

#include <cmath>

namespace GameEngine::Math
{

namespace
{
    /// <summary>축이 화면에서 이보다 짧으면 방향을 말해 주지 못한다. 픽셀 단위다.</summary>
    constexpr float MinimumAxisPixelLength = 0.001f;
}

ViewportPoint ProjectToViewport(
    const Vector3& worldPoint, const Matrix4x4& viewProjection,
    const float viewportWidth, const float viewportHeight)
{
    // 클립 공간의 w를 직접 구한다: TransformPoint는 나눗셈까지 마친 값을 주므로, 카메라 뒤의
    // 점이 앞의 점과 구별되지 않는다.
    const float x = worldPoint.GetX();
    const float y = worldPoint.GetY();
    const float z = worldPoint.GetZ();
    const float clipW = x * viewProjection.GetElement(0, 3) + y * viewProjection.GetElement(1, 3) +
        z * viewProjection.GetElement(2, 3) + viewProjection.GetElement(3, 3);

    ViewportPoint result;
    if (clipW <= 0.0f)
    {
        return result;
    }

    const Vector3 ndc = viewProjection.TransformPoint(worldPoint);
    // NDC는 왼쪽 -1, 위쪽 +1이다. 픽셀은 좌상단이 원점이고 아래가 +y다.
    result.x = (ndc.GetX() * 0.5f + 0.5f) * viewportWidth;
    result.y = (0.5f - ndc.GetY() * 0.5f) * viewportHeight;
    result.isInFront = true;
    return result;
}

float ComputeAxisDragDistance(
    const Vector3& axisOrigin, const Vector3& axisDirection, const Matrix4x4& viewProjection,
    const float viewportWidth, const float viewportHeight, const float dragDeltaPixelsX,
    const float dragDeltaPixelsY)
{
    const ViewportPoint origin =
        ProjectToViewport(axisOrigin, viewProjection, viewportWidth, viewportHeight);
    const ViewportPoint tip = ProjectToViewport(
        axisOrigin + axisDirection, viewProjection, viewportWidth, viewportHeight);
    if (!origin.isInFront || !tip.isInFront)
    {
        return 0.0f;
    }

    // 월드 1단위가 화면에서 그리는 벡터다. 커서 이동을 여기에 정사영한 길이가 곧 월드 거리다.
    const float axisPixelX = tip.x - origin.x;
    const float axisPixelY = tip.y - origin.y;
    const float axisLengthSquared = axisPixelX * axisPixelX + axisPixelY * axisPixelY;
    if (axisLengthSquared < MinimumAxisPixelLength * MinimumAxisPixelLength)
    {
        return 0.0f;
    }
    return (dragDeltaPixelsX * axisPixelX + dragDeltaPixelsY * axisPixelY) / axisLengthSquared;
}

float SnapToGrid(const float value, const float spacing)
{
    if (!(spacing > 0.0f))
    {
        return value;
    }
    return std::round(value / spacing) * spacing;
}

}
