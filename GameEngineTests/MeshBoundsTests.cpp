#include <array>
#include <span>
#include <iostream>

#include "../GameEngine/Assets/MeshData.h"
#include "../GameEngine/Math/Aabb3D.h"

#include "MeshBoundsTests.h"
#include "TestSupport.h"

using GameEngine::Assets::ComputeBounds;
using GameEngine::Math::Aabb3D;
using GameEngine::Core::MeshVertex;
using GameEngine::Math::Vector3;
using TestSupport::Expect;

namespace
{

/// <summary>자리만 정한 정점이다. 법선과 uv는 상자와 무관하므로 0이다.</summary>
[[nodiscard]] constexpr MeshVertex At(const float x, const float y, const float z)
{
    return MeshVertex{ { x, y, z }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } };
}

}

bool RunMeshBoundsTests()
{
    std::cout << "running mesh bounds tests\n";
    bool passed = true;

    // ---- 「비었다」는 감쌀 것이 없었다는 뜻뿐이다 ----
    {
        passed = Expect(
            Aabb3D::Empty().IsEmpty(), "a box that has taken no point should be empty") && passed;
        passed = Expect(
            ComputeBounds(std::span<const MeshVertex>{}).IsEmpty(),
            "a shape with no vertices should give an empty box") && passed;

        // 점 하나짜리 상자는 크기가 0이지만 비어 있지 않다. 비었다고 답하면 그 형상은 거를
        // 근거를 잃는다.
        const std::array oneVertex{ At(3.0f, -4.0f, 5.0f) };
        const Aabb3D point = ComputeBounds(oneVertex);
        passed = Expect(!point.IsEmpty(), "a single vertex should not read as an empty box")
            && passed;
        passed = Expect(
            point.min == Vector3{ 3.0f, -4.0f, 5.0f } && point.max == point.min,
            "a single vertex should give a box standing exactly on it") && passed;
        passed = Expect(
            point.GetSize() == Vector3{ 0.0f, 0.0f, 0.0f },
            "a single vertex should give a box of no size") && passed;
    }

    // ---- 납작한 형상 ----
    //
    // 이 엔진에서 가장 흔한 메시는 평면 위의 사각형이고, 그것은 z가 전부 같다. 두께가 없는
    // 것을 빈 것으로 읽으면 그 메시들이 전부 사라진다.
    {
        const std::array quad{
            At(0.0f, 0.0f, 0.0f), At(2.0f, 0.0f, 0.0f), At(2.0f, 1.0f, 0.0f),
            At(0.0f, 1.0f, 0.0f) };
        const Aabb3D flat = ComputeBounds(quad);
        passed = Expect(!flat.IsEmpty(), "a flat shape should not read as an empty box") && passed;
        passed = Expect(
            flat.GetSize() == Vector3{ 2.0f, 1.0f, 0.0f },
            "a flat shape should keep its two real axes and a third of zero") && passed;
    }

    // ---- 상자는 축마다 따로, 그리고 꼭 맞게 ----
    //
    // x가 가장 작은 정점과 y가 가장 작은 정점을 <b>서로 다른 것</b>으로 둔다. 한 정점을 통째로
    // 골라 모서리로 삼는 구현은 여기서 걸린다.
    {
        const std::array spread{
            At(-5.0f, 2.0f, 0.0f), At(1.0f, -3.0f, 7.0f), At(0.0f, 0.0f, -1.0f) };
        const Aabb3D box = ComputeBounds(spread);
        passed = Expect(
            box.min == Vector3{ -5.0f, -3.0f, -1.0f } && box.max == Vector3{ 1.0f, 2.0f, 7.0f },
            "each axis should take its own smallest and largest vertex") && passed;

        // 이미 안에 있는 점은 상자를 넓히지 않는다.
        Aabb3D grown = box;
        grown.Encapsulate({ 0.5f, 0.5f, 0.5f });
        passed = Expect(grown == box, "a point already inside should not grow the box") && passed;
    }

    // ---- 원점에서 시작하지 않는다 ----
    //
    // 빈 상자 대신 0으로 시작하는 구현은 언제나 원점을 품는다. 그러면 원점에서 멀리 떨어진
    // 형상의 상자가 실제보다 훨씬 커지고, 거르지 못하는 대신 조용히 틀린 답을 낸다.
    {
        const std::array farAway{ At(10.0f, 10.0f, 10.0f), At(12.0f, 14.0f, 11.0f) };
        const Aabb3D box = ComputeBounds(farAway);
        passed = Expect(
            box.min == Vector3{ 10.0f, 10.0f, 10.0f },
            "a shape away from the origin should not drag the origin into its box") && passed;
        passed = Expect(
            box.GetCenter() == Vector3{ 11.0f, 12.0f, 10.5f },
            "the centre should be the middle of the shape, not of the shape and the origin")
            && passed;
    }

    // ---- 두 상자를 합치기 ----
    {
        const Aabb3D left{ { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } };
        const Aabb3D right{ { 2.0f, -1.0f, 0.5f }, { 3.0f, 0.0f, 4.0f } };
        passed = Expect(
            Aabb3D::Empty().UnitedWith(left) == left && left.UnitedWith(Aabb3D::Empty()) == left,
            "uniting with an empty box should change nothing") && passed;
        passed = Expect(
            left.UnitedWith(right) ==
                Aabb3D{ { 0.0f, -1.0f, 0.0f }, { 3.0f, 1.0f, 4.0f } },
            "uniting two boxes should give the smallest box holding both") && passed;
    }

    // ---- 임포터가 담는 자리 ----
    //
    // 형상과 상자가 같은 곳에 산다는 것 자체를 고정한다. 기본값이 빈 상자인 것은 「아직 아무도
    // 계산하지 않았다」와 「감쌀 것이 없었다」가 같은 답이어야 하기 때문이다.
    {
        const GameEngine::Assets::MeshData fresh;
        passed = Expect(
            fresh.bounds.IsEmpty(), "a mesh nobody has filled should carry an empty box") && passed;
    }

    return passed;
}

static const TestSupport::Registration gMeshBoundsTests{
    "AssetDatabase", "mesh bounds tests should pass", RunMeshBoundsTests };
