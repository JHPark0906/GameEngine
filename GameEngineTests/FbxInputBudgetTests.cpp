#include "FbxInputBudgetTests.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/FbxImporter.h"
#include "TestSupport.h"

namespace
{
    void Append32(std::vector<std::byte>& bytes, const std::uint32_t value)
    {
        for (unsigned int shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }

    std::vector<std::byte> Header()
    {
        constexpr std::string_view signature("Kaydara FBX Binary  \0\x1a\0", 23);
        std::vector<std::byte> bytes;
        for (const char c : signature) bytes.push_back(static_cast<std::byte>(c));
        Append32(bytes, 7400);
        return bytes;
    }

    void NodeHeader(std::vector<std::byte>& bytes, const std::uint32_t end,
        const std::uint32_t properties, const std::uint32_t propertyBytes)
    {
        Append32(bytes, end);
        Append32(bytes, properties);
        Append32(bytes, propertyBytes);
        bytes.push_back(std::byte{1});
        bytes.push_back(std::byte{'X'});
    }

    bool Rejects(const std::vector<std::byte>& bytes, const std::string_view expected)
    {
        std::vector<GameEngine::Assets::ImportedMesh> meshes;
        std::string error;
        const bool loaded = GameEngine::Assets::FbxImporter::Load(bytes, meshes, error);
        return TestSupport::Expect(!loaded && error.find(expected) != std::string::npos,
            "malformed FBX should be rejected by the intended parser budget");
    }
}

bool RunFbxInputBudgetTests()
{
    auto nested = Header();
    constexpr std::uint32_t depth = 160;
    for (std::uint32_t index = 0; index < depth; ++index)
        NodeHeader(nested, 27 + 14 * depth + 13 * (depth - index), 0, 0);
    nested.resize(nested.size() + 13 * depth, std::byte{0});
    bool passed = Rejects(nested, "nesting is too deep");

    auto excessiveProperties = Header();
    NodeHeader(excessiveProperties, 54, 0xffffffffu, 0);
    excessiveProperties.resize(54, std::byte{0});
    passed = Rejects(excessiveProperties, "property count") && passed;

    auto excessiveArray = Header();
    NodeHeader(excessiveArray, 67, 1, 13);
    excessiveArray.push_back(std::byte{'f'});
    Append32(excessiveArray, 0xffffffffu);
    Append32(excessiveArray, 1);
    Append32(excessiveArray, 0);
    excessiveArray.resize(67, std::byte{0});
    passed = Rejects(excessiveArray, "memory limit") && passed;

    // A child cannot point into a later top-level record, even if it stays inside the file.
    auto crossingChild = Header();
    NodeHeader(crossingChild, 70, 0, 0);
    NodeHeader(crossingChild, 90, 0, 0);
    crossingChild.resize(90, std::byte{0});
    passed = Rejects(crossingChild, "node bounds") && passed;
    return passed;
}

static const TestSupport::Registration gFbxInputBudgetTests{
    "AssetDatabase", "FBX parsing bounds recursion and declared allocations", RunFbxInputBudgetTests };
