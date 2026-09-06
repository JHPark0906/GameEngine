#include "pch.h"
#include "ClipSpace.h"

namespace GameEngine::Rendering
{

Math::Matrix4x4 MakeClipSpaceCorrection(const ClipSpaceConvention target)
{
    if (target == EngineClipSpace)
    {
        return Math::Matrix4x4::Identity();
    }

    // Flipping +Y is a negative scale on y. Widening depth from [0, 1] to [-1, 1] is z * 2 - 1,
    // which is a scale of two on z followed by a translation of minus one. Both are expressed as
    // one matrix so a backend applies a single multiply whatever combination it needs.
    const float yScale = target.yAxis == ClipSpaceYAxis::Down ? -1.0f : 1.0f;
    const bool widenDepth = target.depthRange == ClipSpaceDepthRange::MinusOneToOne;
    const float depthScale = widenDepth ? 2.0f : 1.0f;
    const float depthOffset = widenDepth ? -1.0f : 0.0f;

    return Math::Matrix4x4::CreateScale({ 1.0f, yScale, depthScale }) *
        Math::Matrix4x4::CreateTranslation({ 0.0f, 0.0f, depthOffset });
}

void ApplyClipSpaceCorrection(
    Math::Matrix4x4& worldViewProjection, const ClipSpaceConvention target)
{
    if (target == EngineClipSpace)
    {
        return;
    }

    worldViewProjection = worldViewProjection * MakeClipSpaceCorrection(target);
}

}
