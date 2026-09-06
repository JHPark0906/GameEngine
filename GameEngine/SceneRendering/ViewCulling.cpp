#include "pch.h"
#include "ViewCulling.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "../Rendering/RenderFrame.h"

namespace GameEngine::SceneRendering
{
namespace
{
    using HomogeneousPoint = std::array<double, 4>;
    using Point = std::array<double, 3>;
    constexpr double Tolerance = 16.0 * std::numeric_limits<float>::epsilon();

    [[nodiscard]] HomogeneousPoint TransformPoint(
        const Math::Matrix4x4& matrix, const double x, const double y, const double z)
    {
        HomogeneousPoint result{};
        for (std::size_t column = 0; column < 4; ++column)
            result[column] = x * matrix.GetElement(0, column) + y * matrix.GetElement(1, column) +
                z * matrix.GetElement(2, column) + matrix.GetElement(3, column);
        return result;
    }

    [[nodiscard]] bool IsFinite(const HomogeneousPoint& point)
    {
        return std::all_of(point.begin(), point.end(), [](const double value) { return std::isfinite(value); });
    }
}

ViewCulling::ViewCulling(const Rendering::CameraRenderData& camera, const bool clipDepth)
    : mViewProjection(camera.view * camera.projection), mClipDepth(clipDepth)
{
    Math::Matrix4x4 inverse;
    if (camera.view.IsFinite() && camera.projection.IsFinite() && mViewProjection.IsFinite() &&
        mViewProjection.TryInvert(inverse) && inverse.IsFinite()) mClipToWorld = inverse;
}

bool ViewCulling::IsVisible(const Math::Aabb3D& localBounds, const Math::Matrix4x4& localToWorld) const
{
    if (!mClipToWorld || !localToWorld.IsFinite() || !localBounds.min.IsFinite() || !localBounds.max.IsFinite())
        return true;
    if (localBounds.IsEmpty()) return false;
    const auto localToClip = localToWorld * mViewProjection;
    if (!localToClip.IsFinite()) return true;

    // A box may enclose the view without placing any corner inside it. Reject only when
    // one common plane excludes every corner, rather than requiring an inside corner.
    std::array<bool, 6> allOutside{ true, true, true, true, mClipDepth, mClipDepth };
    for (unsigned int corner = 0; corner < 8; ++corner)
    {
        const auto clip = TransformPoint(localToClip,
            (corner & 1u) ? localBounds.max.GetX() : localBounds.min.GetX(),
            (corner & 2u) ? localBounds.max.GetY() : localBounds.min.GetY(),
            (corner & 4u) ? localBounds.max.GetZ() : localBounds.min.GetZ());
        if (!IsFinite(clip)) return true;
        // Test homogeneous inequalities directly; dividing by negative w would put objects
        // behind a perspective camera back inside its apparent screen rectangle.
        const std::array<double, 6> distances{
            clip[0] + clip[3], clip[3] - clip[0], clip[1] + clip[3], clip[3] - clip[1],
            clip[2], clip[3] - clip[2] };
        const double tolerance = Tolerance * (std::max)({ 1.0, std::abs(clip[0]), std::abs(clip[1]),
            std::abs(clip[2]), std::abs(clip[3]) });
        for (std::size_t plane = 0; plane < allOutside.size(); ++plane)
            allOutside[plane] = allOutside[plane] && distances[plane] < -tolerance;
    }
    return std::none_of(allOutside.begin(), allOutside.end(), [](const bool outside) { return outside; });
}

std::optional<Math::Aabb2D> ViewCulling::GetVisiblePlaneBounds(const Math::Matrix4x4& localToWorld) const
{
    Math::Matrix4x4 worldToLocal;
    if (!mClipToWorld || !localToWorld.IsFinite() || !localToWorld.TryInvert(worldToLocal) || !worldToLocal.IsFinite())
        return std::nullopt;
    const auto clipToLocal = *mClipToWorld * worldToLocal;
    if (!clipToLocal.IsFinite()) return std::nullopt;

    std::array<Point, 8> corners{};
    double zMagnitude = 1.0;
    bool negativeW = false;
    for (unsigned int index = 0; index < 8; ++index)
    {
        const auto point = TransformPoint(clipToLocal, (index & 1u) ? 1.0 : -1.0,
            (index & 2u) ? 1.0 : -1.0, (index & 4u) ? 1.0 : 0.0);
        if (!IsFinite(point) || point[3] == 0.0) return std::nullopt;
        if (index == 0) negativeW = point[3] < 0.0;
        else if ((point[3] < 0.0) != negativeW) return std::nullopt; // An edge crosses infinity.
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            corners[index][axis] = point[axis] / point[3];
            if (!std::isfinite(corners[index][axis])) return std::nullopt;
        }
        zMagnitude = (std::max)(zMagnitude, std::abs(corners[index][2]));
    }

    double minX = std::numeric_limits<double>::infinity();
    double minY = minX;
    double maxX = -minX;
    double maxY = -minX;
    bool intersects = false;
    const auto include = [&](const Point& point)
    {
        minX = (std::min)(minX, point[0]);
        minY = (std::min)(minY, point[1]);
        maxX = (std::max)(maxX, point[0]);
        maxY = (std::max)(maxY, point[1]);
        intersects = true;
    };
    const double planeTolerance = Tolerance * zMagnitude;
    if (!mClipDepth)
    {
        // An orthographic view without near/far clipping is an infinite prism. Intersect its
        // four parallel corner lines with the tile plane, not their finite near/far segments.
        for (unsigned int index = 0; index < 4; ++index)
        {
            const auto& first = corners[index];
            const auto& second = corners[index + 4];
            const double deltaZ = second[2] - first[2];
            if (std::abs(deltaZ) <= planeTolerance) return std::nullopt;
            const double fraction = -first[2] / deltaZ;
            include({ first[0] + (second[0] - first[0]) * fraction,
                first[1] + (second[1] - first[1]) * fraction, 0.0 });
        }
    }
    // Three bit directions from each corner enumerate each of the 12 frustum edges once.
    // Vertices already on the plane cover coincident near/far faces and tangent edges too.
    for (unsigned int index = 0; mClipDepth && index < 8; ++index)
    {
        const auto& first = corners[index];
        if (std::abs(first[2]) <= planeTolerance) include(first);
        for (unsigned int axis = 0; axis < 3; ++axis)
        {
            const unsigned int bit = 1u << axis;
            if ((index & bit) != 0) continue;
            const auto& second = corners[index | bit];
            if ((first[2] < 0.0 && second[2] > 0.0) || (first[2] > 0.0 && second[2] < 0.0))
            {
                const double fraction = first[2] / (first[2] - second[2]);
                include({ first[0] + (second[0] - first[0]) * fraction,
                    first[1] + (second[1] - first[1]) * fraction, 0.0 });
            }
        }
    }
    if (!intersects) return Math::Aabb2D{};
    // Round outward before narrowing to float so a tile touching the view boundary
    // remains a candidate despite inverse-transform and intersection roundoff.
    const double padX = Tolerance * (std::max)({ 1.0, std::abs(minX), std::abs(maxX) });
    const double padY = Tolerance * (std::max)({ 1.0, std::abs(minY), std::abs(maxY) });
    constexpr double maximumFloat = std::numeric_limits<float>::max();
    if (minX - padX < -maximumFloat || minY - padY < -maximumFloat ||
        maxX + padX > maximumFloat || maxY + padY > maximumFloat) return std::nullopt;
    const Math::Aabb2D bounds{
        { static_cast<float>(minX - padX), static_cast<float>(minY - padY) },
        { static_cast<float>(maxX + padX), static_cast<float>(maxY + padY) } };
    if (!std::isfinite(bounds.min.GetX()) || !std::isfinite(bounds.min.GetY()) ||
        !std::isfinite(bounds.max.GetX()) || !std::isfinite(bounds.max.GetY())) return std::nullopt;
    return bounds;
}

}
