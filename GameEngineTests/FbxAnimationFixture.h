#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace TestSupport::FbxFixture
{

using Bytes = std::vector<std::byte>;

struct FixtureOptions
{
    bool animated = true;
    bool animateParent = true;
    bool skin = false;
    float angle = 1080.0f;
    std::int32_t flags = 1032;
    std::int32_t references = 2;
    double geometricTranslation = 2.0;
};

/// <summary>Builds an authored binary FBX fixture; no external model or asset bytes are used.</summary>
[[nodiscard]] Bytes MakeFixture(FixtureOptions options = {});

/// <summary>One weighted bone, an unclustered parent transform, and one animated clip.</summary>
[[nodiscard]] Bytes MakeSkinnedFixture();

}
