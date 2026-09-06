#include "ViewCullingTests.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

#include "Math/Aabb2D.h"
#include "Math/Aabb3D.h"
#include "Math/Matrix.h"
#include "Rendering/RenderFrame.h"
#include "SceneRendering/ViewCulling.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using Math::Matrix4x4;
    using SceneRendering::ViewCulling;
    using TestSupport::Expect;

    [[nodiscard]] Rendering::CameraRenderData Camera(const bool perspective)
    {
        Rendering::CameraRenderData camera;
        camera.view = Matrix4x4::CreateTranslation({ 0, 0, 5 });
        camera.projection = perspective ? Matrix4x4::CreatePerspectiveFieldOfViewLeftHanded(90, 2, 1, 11) :
            Matrix4x4::CreateOrthographicLeftHanded(8, 4, 1, 11);
        return camera;
    }

    [[nodiscard]] bool NearBounds(const std::optional<Math::Aabb2D>& actual,
        const Math::Aabb2D& expected, const float tolerance = .001f)
    {
        return actual && !actual->IsEmpty() &&
            std::abs(actual->min.GetX() - expected.min.GetX()) < tolerance &&
            std::abs(actual->min.GetY() - expected.min.GetY()) < tolerance &&
            std::abs(actual->max.GetX() - expected.max.GetX()) < tolerance &&
            std::abs(actual->max.GetY() - expected.max.GetY()) < tolerance;
    }

    bool ClipPlanesAndDepth()
    {
        bool passed = true;
        const auto unit = Math::Aabb3D::FromCenterSize({}, { .2f, .2f, .2f });
        for (const bool perspective : { false, true })
        {
            const ViewCulling culling(Camera(perspective));
            passed &= Expect(culling.IsVisible(unit, Matrix4x4::Identity()), "the view center must survive culling");
            for (const Math::Vector3& outside : std::array<Math::Vector3, 6>{
                Math::Vector3{ -100, 0, 0 }, { 100, 0, 0 }, { 0, -100, 0 }, { 0, 100, 0 },
                { 0, 0, -4.5f }, { 0, 0, 7 } })
                passed &= Expect(!culling.IsVisible(unit, Matrix4x4::CreateTranslation(outside)),
                    "each of the four side planes and D3D near/far planes must reject wholly outside boxes");
            passed &= Expect(!culling.IsVisible(unit, Matrix4x4::CreateTranslation({ 0, 0, -6 })),
                "a box behind the camera must not reappear after a negative-w perspective divide");
            passed &= Expect(culling.IsVisible(unit, Matrix4x4::CreateTranslation({ 0, 0, -4 })),
                "a box crossing the near plane must remain visible");
            passed &= Expect(culling.IsVisible(Math::Aabb3D::FromCenterSize({}, { 1000, 1000, 1000 }),
                Matrix4x4::Identity()), "a box enclosing the frustum must survive even when none of its corners is inside");
            const auto plane = culling.GetVisiblePlaneBounds(Matrix4x4::Identity());
            passed &= Expect(NearBounds(plane, perspective ? Math::Aabb2D{ { -10, -5 }, { 10, 5 } } :
                Math::Aabb2D{ { -4, -2 }, { 4, 2 } }),
                "tile bounds must match the actual camera footprint on local z=0");
            for (const float depth : { -6.0f, -4.5f, 7.0f })
            {
                const auto hidden = culling.GetVisiblePlaneBounds(Matrix4x4::CreateTranslation({ 0, 0, depth }));
                passed &= Expect(hidden && hidden->IsEmpty(),
                    "a tile plane behind the view, before near, or beyond far must produce an empty range");
            }
            const auto onNear = culling.GetVisiblePlaneBounds(Matrix4x4::CreateTranslation({ 0, 0, -4 }));
            passed &= Expect(NearBounds(onNear, perspective ? Math::Aabb2D{ { -2, -1 }, { 2, 1 } } :
                Math::Aabb2D{ { -4, -2 }, { 4, 2 } }),
                "a tile plane coincident with the near face must retain that entire face");
        }
        auto offCenter = Camera(false);
        offCenter.projection = Matrix4x4::CreateOrthographicOffCenterLeftHanded(-3, 7, -2, 4, 1, 11);
        passed &= Expect(NearBounds(ViewCulling(offCenter).GetVisiblePlaneBounds(Matrix4x4::Identity()),
            { { -3, -2 }, { 7, 4 } }), "off-center projections must retain their asymmetric visible footprint");
        return passed;
    }

    bool RotationAndHierarchy()
    {
        bool passed = true;
        auto rolled = Camera(false);
        const auto rolledPose = Matrix4x4::CreateTransform({ 0, 0, -5 }, { 0, 0, 45 }, { 1, 1, 1 });
        if (!Expect(rolledPose.TryInvert(rolled.view), "a rotated camera pose must be invertible")) return false;
        const float extent = 6.0f / std::sqrt(2.0f);
        passed &= Expect(NearBounds(ViewCulling(rolled).GetVisiblePlaneBounds(Matrix4x4::Identity()),
            { { -extent, -extent }, { extent, extent } }), "camera roll must rotate the visible tile footprint");

        // Here all four near-to-far corner rays are parallel to z=0. The intersection lies on
        // near/far face edges, so an implementation that checks only four rays misses it.
        for (const bool perspective : { false, true })
        {
            auto side = Camera(perspective);
            const auto sidePose = Matrix4x4::CreateTransform({ 10, 2, 0 }, { 0, 90, 0 }, { 1, 1, 1 });
            if (!Expect(sidePose.TryInvert(side.view), "the side-facing camera must be invertible")) return false;
            const ViewCulling culling(side);
            passed &= Expect(NearBounds(culling.GetVisiblePlaneBounds(Matrix4x4::Identity()),
                perspective ? Math::Aabb2D{ { 11, -9 }, { 21, 13 } } : Math::Aabb2D{ { 11, 0 }, { 21, 4 } }),
                "all twelve frustum edges must contribute to a side-on tile-plane intersection");
            passed &= Expect(culling.IsVisible(Math::Aabb3D::FromCenterSize({}, { .2f, .2f, .2f }),
                Matrix4x4::CreateTranslation({ 15, 2, 0 })) &&
                !culling.IsVisible(Math::Aabb3D::FromCenterSize({}, { .2f, .2f, .2f }), Matrix4x4::Identity()),
                "rotated cameras must cull from their view matrix rather than the game's XY camera center");
        }

        const auto child = Matrix4x4::CreateTransform({ 1, -2, 0 }, { 0, 0, 30 }, { 1.5f, -.75f, 1 });
        const auto parent = Matrix4x4::CreateTransform({ 40, -30, 0 }, { 0, 0, -20 }, { -2, 3, 1 });
        const auto localToWorld = child * parent;
        Matrix4x4 inverse;
        if (!Expect(localToWorld.TryInvert(inverse), "negative/nonuniform parent scales must remain invertible")) return false;
        auto shifted = Camera(false);
        shifted.view = Matrix4x4::CreateTranslation({ -35, 26, 5 });
        const ViewCulling culling(shifted);
        Math::Aabb3D expected = Math::Aabb3D::Empty();
        for (const float x : { 31.0f, 39.0f })
            for (const float y : { -28.0f, -24.0f }) expected.Encapsulate(inverse.TransformPoint({ x, y, 0 }));
        passed &= Expect(NearBounds(culling.GetVisiblePlaneBounds(localToWorld),
            { { expected.min.GetX(), expected.min.GetY() }, { expected.max.GetX(), expected.max.GetY() } }),
            "tile-local bounds must account for hierarchy-induced shear, translation, and negative scale");
        const auto localCenter = inverse.TransformPoint({ 35, -26, 0 });
        passed &= Expect(culling.IsVisible(Math::Aabb3D::FromCenterSize(localCenter, { .1f, .1f, .1f }), localToWorld),
            "a visible local box must survive a mirrored nonuniform parent hierarchy");
        const auto outside = inverse.TransformPoint({ 60, -26, 0 });
        passed &= Expect(!culling.IsVisible(Math::Aabb3D::FromCenterSize(outside, { .1f, .1f, .1f }), localToWorld),
            "the same parent hierarchy must still cull a box wholly beyond the view");

        const auto tilted = Matrix4x4::CreateRotationYDegrees(35);
        const float halfWidth = 4.0f / std::cos(35.0f * 3.14159265358979323846f / 180.0f);
        passed &= Expect(NearBounds(ViewCulling(Camera(false)).GetVisiblePlaneBounds(tilted),
            { { -halfWidth, -2 }, { halfWidth, 2 } }), "a tilted tile plane must use its own local axes");
        return passed;
    }

    bool InvalidInputsRemainConservative()
    {
        bool passed = true;
        const auto unit = Math::Aabb3D::FromCenterSize({}, { 1, 1, 1 });
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        const auto infinity = std::numeric_limits<float>::infinity();
        const std::array invalid{ Matrix4x4::CreateTranslation({ nan, 0, 0 }),
            Matrix4x4::CreateScale({ 1, infinity, 1 }) };
        const ViewCulling valid(Camera(false));
        for (const auto& matrix : invalid)
        {
            passed &= Expect(valid.IsVisible(unit, matrix) && !valid.GetVisiblePlaneBounds(matrix),
                "invalid object matrices must preserve draws and request a full tile-range fallback");
            auto camera = Camera(true);
            camera.projection = matrix;
            const ViewCulling invalidView(camera);
            passed &= Expect(invalidView.IsVisible(unit, Matrix4x4::Identity()) &&
                !invalidView.GetVisiblePlaneBounds(Matrix4x4::Identity()),
                "invalid camera matrices must never manufacture an empty visible range");
        }
        passed &= Expect(valid.IsVisible({ { nan, 0, 0 }, { 1, 1, 1 } }, Matrix4x4::Identity()),
            "nonfinite source bounds must fail open instead of dropping a potentially visible draw");
        passed &= Expect(!valid.IsVisible(Math::Aabb3D::Empty(), Matrix4x4::Identity()),
            "an explicitly empty source box has no geometry to draw");
        auto singularCamera = Camera(false);
        singularCamera.projection = Matrix4x4::CreateScale({ 0, 1, 1 });
        const ViewCulling noCameraInverse(singularCamera);
        passed &= Expect(noCameraInverse.IsVisible(unit, Matrix4x4::Identity()) &&
            !noCameraInverse.GetVisiblePlaneBounds(Matrix4x4::Identity()),
            "a singular camera matrix must request conservative draw and tile-range fallback");
        const auto flattened = Matrix4x4::CreateScale({ 1, 1, 0 });
        passed &= Expect(!valid.GetVisiblePlaneBounds(flattened) && valid.IsVisible(unit, flattened) &&
            !valid.IsVisible(Math::Aabb3D::FromCenterSize({ 30, 0, 0 }, { 1, 1, 0 }), flattened),
            "a flattened XY tilemap must still cull individual offscreen cells when its range inverse is unavailable");
        return passed;
    }
}

bool RunViewCullingTests()
{
    return ClipPlanesAndDepth() && RotationAndHierarchy() && InvalidInputsRemainConservative();
}

static const TestSupport::Registration gViewCullingTests{
    "RenderFrame", "separate view cameras must cull boxes and bound tile-plane intersections", RunViewCullingTests };
