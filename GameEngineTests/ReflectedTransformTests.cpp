#include "ReflectedTransformTests.h"

#include <array>
#include <cmath>
#include <cstddef>

#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Runtime/Transform.h"
#include "TestSupport.h"

namespace
{
namespace Math = GameEngine::Math;

bool SameMatrix(const Math::Matrix4x4& first, const Math::Matrix4x4& second)
{
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            if (std::abs(first.GetElement(row, column) - second.GetElement(row, column)) > 0.001f)
                return false;
    return true;
}
}

bool RunReflectedTransformTests()
{
    bool passed = true;
    const std::array rotations{ Math::Vector3{ 25.0f, -40.0f, 15.0f },
        Math::Vector3{ 90.0f, 30.0f, 0.0f }, Math::Vector3{ -90.0f, 30.0f, 0.0f } };
    for (int signs = 0; signs < 8; ++signs)
    {
        const Math::Vector3 scale{ (signs & 1) ? -2.0f : 2.0f,
            (signs & 2) ? -0.5f : 0.5f, (signs & 4) ? -3.0f : 3.0f };
        for (const auto& rotation : rotations)
        {
            const auto original = Math::Matrix4x4::CreateTransform({ 1.0f, -2.0f, 3.0f }, rotation, scale);
            Math::Vector3 outScale, outRotation, outPosition;
            original.Decompose(outScale, outRotation, outPosition);
            passed = TestSupport::Expect(SameMatrix(original,
                Math::Matrix4x4::CreateTransform(outPosition, outRotation, outScale)),
                "all scale sign combinations must preserve their full transform after decomposition") && passed;
        }
    }

    GameEngine::Runtime::Transform parent;
    parent.SetPosition({ 2.0f, 3.0f, -1.0f });
    parent.SetRotation({ 10.0f, 15.0f, 20.0f });
    parent.SetScale({ 2.0f, 2.0f, 2.0f });
    GameEngine::Runtime::Transform child;
    child.SetScale({ -1.0f, 2.0f, 3.0f });
    child.SetRotation({ 5.0f, 10.0f, 15.0f });
    static_cast<void>(child.SetParent(&parent));
    const auto before = child.GetLocalToWorldMatrix();
    passed = TestSupport::Expect(child.SetParent(nullptr, true) &&
        SameMatrix(before, child.GetLocalToWorldMatrix()) &&
        child.GetLocalToWorldMatrix().GetDeterminant() < 0.0f,
        "world-preserving reparenting must keep a reflected object's position, basis and winding") && passed;
    return passed;
}

static const TestSupport::Registration gReflectedTransformTests{
    "Math", "reflected transforms should survive decomposition and reparenting", RunReflectedTransformTests };
