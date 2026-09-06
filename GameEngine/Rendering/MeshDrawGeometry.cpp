#include "pch.h"
#include "MeshDrawGeometry.h"
#include "../Diagnostics/Debug.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace GameEngine::Rendering
{
namespace
{
    [[nodiscard]] Math::Matrix4x4 SingularNormalTransform(const Math::Matrix4x4& world)
    {
        // A singular transform can still preserve a triangle: flattening its unused axis does
        // not erase the surface. Cofactors give that surface's normal even without an inverse.
        // Compute and rescale in double so large or tiny finite scales remain representable.
        std::array<std::array<double, 3>, 3> rows{};
        std::array<std::array<double, 3>, 3> cofactors{};
        for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            rows[row][column] = world.GetElement(row, column);
        double largest = 0;
        for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
        {
            const double value = rows[(row + 1) % 3][(column + 1) % 3] *
                rows[(row + 2) % 3][(column + 2) % 3] -
                rows[(row + 1) % 3][(column + 2) % 3] * rows[(row + 2) % 3][(column + 1) % 3];
            cofactors[row][column] = value;
            largest = (std::max)(largest, std::abs(value));
        }
        if (largest == 0) return Math::Matrix4x4::Identity(); // Every triangle collapsed to a line or point.
        const double determinant = rows[0][0] * cofactors[0][0] + rows[0][1] * cofactors[0][1] +
            rows[0][2] * cofactors[0][2];
        const double factor = (determinant < 0 ? -1.0 : 1.0) / largest;
        const auto normalRow = [&](const std::size_t row)
        {
            return Math::Vector3{ static_cast<float>(cofactors[row][0] * factor),
                static_cast<float>(cofactors[row][1] * factor),
                static_cast<float>(cofactors[row][2] * factor) };
        };
        return Math::Matrix4x4::FromRows(normalRow(0), normalRow(1), normalRow(2), {});
    }
}

std::optional<MeshShading> TryBuildMeshShading(
    const RenderFrame& frame,
    const Math::Matrix4x4& localToWorld,
    const char* const backendName)
{
    // The frontend always supplies camera state, including the fallback for a scene with no camera,
    // so a backend must never substitute a projection of its own.
    const std::optional<CameraRenderData>& camera = frame.GetCamera();
    if (!camera)
    {
        Diagnostics::Debug::LogError(
            backendName, " cannot render a mesh without frame camera state.");
        return std::nullopt;
    }

    MeshShading shading;
    Math::Matrix4x4 inverseWorld;
    shading.world = localToWorld;
    shading.normalToWorld = localToWorld.TryInvert(inverseWorld)
        ? inverseWorld.Transpose() : SingularNormalTransform(localToWorld);
    shading.worldViewProjection = localToWorld * camera->view * camera->projection;
    shading.ambientLight = frame.GetAmbientLight();
    const std::vector<LightRenderData>& lights = frame.GetLights();
    shading.lightCount = (std::min)(lights.size(), shading.lights.size());
    std::copy_n(lights.begin(), shading.lightCount, shading.lights.begin());
    return shading;
}

void StoreMeshLighting(const MeshShading& shading, MeshConstants& constants)
{
    constants.tint = { shading.tint.r, shading.tint.g, shading.tint.b, shading.tint.a };
    constants.ambientAndLightCount = {
        shading.ambientLight.r, shading.ambientLight.g, shading.ambientLight.b,
        static_cast<float>(shading.lightCount) };
    static_assert(ShaderMaxLights == MaxFrameLights,
        "The shader must be able to read every light a frame can carry.");
    for (std::size_t index = 0; index < ShaderMaxLights; ++index)
    {
        ShaderLight& target = constants.lights[index];
        if (index >= shading.lightCount)
        {
            target = {};
            continue;
        }
        const LightRenderData& light = shading.lights[index];
        if (light.kind == LightKind::Point)
        {
            target.positionOrDirection = {
                light.position.GetX(), light.position.GetY(), light.position.GetZ(), 1.0f };
        }
        else
        {
            const Math::Vector3 direction = light.direction.Normalized();
            target.positionOrDirection = { direction.GetX(), direction.GetY(), direction.GetZ(), 0.0f };
        }
        target.colorAndRange = { light.color.r, light.color.g, light.color.b, light.range };
    }
}

}
