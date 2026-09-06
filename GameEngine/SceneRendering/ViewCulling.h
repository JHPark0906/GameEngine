#pragma once

#include <optional>

#include "../Math/Aabb2D.h"
#include "../Math/Aabb3D.h"
#include "../Math/Matrix.h"

namespace GameEngine::Rendering { struct CameraRenderData; }

namespace GameEngine::SceneRendering
{

/// <summary>별도 뷰 카메라의 실제 view/projection으로 draw와 타일 평면의 가시 범위를 구한다.</summary>
class ViewCulling final
{
public:
    // Scene orthographic culling leaves depth decisions to the GPU and uses only the four
    // side planes. Explicit capture cameras use the complete frustum.
    explicit ViewCulling(const Rendering::CameraRenderData& camera, bool clipDepth = true);

    /// <summary>상자의 모든 꼭짓점이 한 clip 평면 밖에 있을 때만 거른다. 계산 불능은 보이는 것으로 취급한다.</summary>
    [[nodiscard]] bool IsVisible(const Math::Aabb3D& localBounds, const Math::Matrix4x4& localToWorld) const;

    /// <summary>
    /// 절두체와 타일 local z=0 평면의 교차를 감싸는 local XY 영역이다.
    /// 교차가 없으면 빈 상자, 유한한 역변환을 구할 수 없으면 nullopt(전체 범위 사용)다.
    /// </summary>
    [[nodiscard]] std::optional<Math::Aabb2D> GetVisiblePlaneBounds(const Math::Matrix4x4& localToWorld) const;

private:
    Math::Matrix4x4 mViewProjection;
    std::optional<Math::Matrix4x4> mClipToWorld;
    bool mClipDepth = true;
};

}
