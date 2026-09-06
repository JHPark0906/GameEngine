#include "MathTests.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "Math/MathUtility.h"
#include "Math/Matrix.h"
#include "Math/Quaternion.h"
#include "Math/Random.h"
#include "Math/Vector.h"
#include "Math/ViewportProjection.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using namespace GameEngine::Math;

    [[nodiscard]] bool Near(const Vector3& a, const Vector3& b, const float tolerance = 0.0005f)
    {
        return IsNearlyEqual(a.GetX(), b.GetX(), tolerance) &&
            IsNearlyEqual(a.GetY(), b.GetY(), tolerance) &&
            IsNearlyEqual(a.GetZ(), b.GetZ(), tolerance);
    }

    [[nodiscard]] bool Near(const Matrix4x4& a, const Matrix4x4& b, const float tolerance = 0.0005f)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            for (std::size_t column = 0; column < 4; ++column)
            {
                if (!IsNearlyEqual(a.GetElement(row, column), b.GetElement(row, column), tolerance))
                {
                    return false;
                }
            }
        }
        return true;
    }

    /// <summary>벡터 산술은 성분별이고, 내적·외적·길이는 손으로 검산할 수 있는 값이어야 한다.</summary>
    bool RunVectorTests()
    {
        const Vector3 a{ 1.0f, 2.0f, 3.0f };
        const Vector3 b{ 4.0f, 5.0f, 6.0f };
        const bool arithmetic = (a + b) == Vector3{ 5.0f, 7.0f, 9.0f } &&
            (b - a) == Vector3{ 3.0f, 3.0f, 3.0f } &&
            (a * 2.0f) == Vector3{ 2.0f, 4.0f, 6.0f } && (2.0f * a) == (a * 2.0f) &&
            (b / 2.0f) == Vector3{ 2.0f, 2.5f, 3.0f } && (-a) == Vector3{ -1.0f, -2.0f, -3.0f };
        const bool compound = [] {
            Vector3 v{ 1.0f, 1.0f, 1.0f };
            v += Vector3::One;
            v *= 3.0f;
            v -= Vector3::Right;
            return v == Vector3{ 5.0f, 6.0f, 6.0f };
        }();
        const bool dot = IsNearlyEqual(a.Dot(b), 32.0f);
        // 왼손 좌표계의 기저: Right × Up = Forward.
        const bool cross = Vector3::Right.Cross(Vector3::Up) == Vector3::Forward &&
            Vector3::Up.Cross(Vector3::Forward) == Vector3::Right;
        const bool length = IsNearlyEqual(Vector3{ 3.0f, 4.0f, 0.0f }.GetLength(), 5.0f) &&
            IsNearlyEqual(Vector3{ 3.0f, 4.0f, 0.0f }.Normalized().GetLength(), 1.0f) &&
            Vector3::Zero.Normalized() == Vector3::Zero;
        const bool lerp = Vector3::Lerp(a, b, 0.5f) == Vector3{ 2.5f, 3.5f, 4.5f };
        const bool vector2 = (Vector2{ 1.0f, 2.0f } + Vector2{ 3.0f, 4.0f }) == Vector2{ 4.0f, 6.0f } &&
            IsNearlyEqual(Vector2{ 3.0f, 4.0f }.GetLength(), 5.0f) &&
            Vector2Int{ 1, 2 }.ToVector2() == Vector2{ 1.0f, 2.0f };

        return Expect(arithmetic, "vector arithmetic should be component-wise") &&
            Expect(compound, "compound assignment should compose") &&
            Expect(dot, "the dot product should be 32") &&
            Expect(cross, "the cross product should follow the left-handed basis") &&
            Expect(length, "length and normalization should behave, including for zero") &&
            Expect(lerp, "lerp at one half should be the midpoint") &&
            Expect(vector2, "Vector2 should share the same arithmetic");
    }

    /// <summary>쿼터니언과 행렬은 같은 회전 규약을 말해야 한다. 행렬이 정본이다.</summary>
    bool RunQuaternionTests()
    {
        const std::array<Vector3, 5> eulers{
            Vector3{ 0.0f, 0.0f, 0.0f }, Vector3{ 30.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 45.0f, 0.0f },
            Vector3{ 0.0f, 0.0f, 60.0f }, Vector3{ 20.0f, -50.0f, 70.0f } };
        const Vector3 probe{ 0.3f, -0.7f, 1.1f };

        bool matricesAgree = true;
        bool rotationsAgree = true;
        bool eulerRoundTrips = true;
        for (const Vector3& euler : eulers)
        {
            const Matrix4x4 fromEuler = Matrix4x4::CreateRotationRollPitchYawDegrees(euler);
            const Quaternion quaternion = Quaternion::FromEulerDegrees(euler);
            matricesAgree &= Near(quaternion.ToMatrix(), fromEuler);
            rotationsAgree &= Near(quaternion.Rotate(probe), fromEuler.TransformDirection(probe));
            eulerRoundTrips &= Near(quaternion.ToEulerDegrees(), euler, 0.01f);
        }

        // 곱셈 순서는 행렬과 같다: a * b는 a를 먼저 적용한다.
        const Quaternion a = Quaternion::FromEulerDegrees({ 30.0f, 0.0f, 0.0f });
        const Quaternion b = Quaternion::FromEulerDegrees({ 0.0f, 45.0f, 0.0f });
        const bool productOrder = Near((a * b).ToMatrix(), a.ToMatrix() * b.ToMatrix());

        const Quaternion axisAngle = Quaternion::FromAxisAngleDegrees(Vector3::Up, 90.0f);
        const Matrix4x4 yaw90 = Matrix4x4::CreateRotationYDegrees(90.0f);
        const bool axisAngleMatches = Near(axisAngle.ToMatrix(), yaw90) &&
            Near(axisAngle.Rotate(Vector3::Forward), yaw90.TransformDirection(Vector3::Forward));

        const bool inverseCancels = Near((a * a.Inverse()).Rotate(probe), probe);
        const Quaternion fromTo = Quaternion::FromToRotation(Vector3::Forward, Vector3::Right);
        const bool fromToWorks = Near(fromTo.Rotate(Vector3::Forward), Vector3::Right);
        const Quaternion half = Quaternion::Slerp(Quaternion::Identity(), axisAngle, 0.5f);
        const bool slerpHalfway = IsNearlyEqual(half.AngleTo(Quaternion::Identity()), 45.0f, 0.01f) &&
            IsNearlyEqual(half.AngleTo(axisAngle), 45.0f, 0.01f);

        return Expect(matricesAgree, "a quaternion from Euler angles should match the matrix") &&
            Expect(rotationsAgree, "Rotate should match the matrix's TransformDirection") &&
            Expect(eulerRoundTrips, "Euler angles should survive a trip through a quaternion") &&
            Expect(productOrder, "a * b should apply a first, like matrices") &&
            Expect(axisAngleMatches, "an axis-angle quaternion should match the axis rotation matrix") &&
            Expect(inverseCancels, "a rotation times its inverse should be identity") &&
            Expect(fromToWorks, "FromToRotation should take from onto to") &&
            Expect(slerpHalfway, "slerp at one half should sit halfway in angle");
    }

    /// <summary>분해는 합성의 역이어야 한다. Transform의 월드 유지 재부모화가 이것에 기댄다.</summary>
    bool RunMatrixDecompositionTests()
    {
        const Vector3 translation{ 1.0f, -2.0f, 3.0f };
        const Vector3 rotation{ 25.0f, -40.0f, 15.0f };
        const Vector3 scale{ 2.0f, 0.5f, 3.0f };
        const Matrix4x4 composed = Matrix4x4::CreateTransform(translation, rotation, scale);

        Vector3 outScale, outRotation, outTranslation;
        composed.Decompose(outScale, outRotation, outTranslation);
        const bool roundTrips = Near(outScale, scale, 0.001f) && Near(outRotation, rotation, 0.01f) &&
            Near(outTranslation, translation, 0.001f);
        const bool translationRead = Near(composed.GetTranslation(), translation);

        // 분해한 값으로 다시 합성하면 같은 행렬이어야 한다 — 각도의 표현이 달라도 변환은 같다.
        const bool recomposes = Near(
            Matrix4x4::CreateTransform(outTranslation, outRotation, outScale), composed, 0.001f);

        // 짐벌 락(pitch 90도)에서도 변환은 보존된다.
        const Matrix4x4 locked = Matrix4x4::CreateTransform(Vector3::Zero, { 90.0f, 30.0f, 0.0f }, Vector3::One);
        Vector3 s2, r2, t2;
        locked.Decompose(s2, r2, t2);
        const bool lockedRecomposes = Near(Matrix4x4::CreateTransform(t2, r2, s2), locked, 0.001f);

        return Expect(roundTrips, "decomposition should recover scale, rotation, and translation") &&
            Expect(translationRead, "GetTranslation should read the fourth row") &&
            Expect(recomposes, "recomposing decomposed values should reproduce the matrix") &&
            Expect(lockedRecomposes, "decomposition at gimbal lock should still preserve the transform");
    }

    /// <summary>
    /// 난수는 시드에 대해 결정적이어야 하고, 범위 약속을 지켜야 하며, 눈에 띄게 치우치지 않아야
    /// 한다. 분포 검사는 느슨하다 — 여기서 증명하려는 것은 알고리즘의 품질이 아니라 배선이다.
    /// </summary>
    bool RunRandomTests()
    {
        Random first(12345);
        Random second(12345);
        bool deterministic = true;
        for (int i = 0; i < 100; ++i)
        {
            deterministic &= first.NextUInt64() == second.NextUInt64();
        }
        Random third(54321);
        const bool differentSeedsDiffer = Random(12345).NextUInt64() != third.NextUInt64();

        Random random(7);
        bool floatInRange = true;
        bool intInRange = true;
        std::array<int, 6> buckets{};
        float sum = 0.0f;
        constexpr int Samples = 20000;
        for (int i = 0; i < Samples; ++i)
        {
            const float value = random.NextFloat();
            floatInRange &= value >= 0.0f && value < 1.0f;
            sum += value;
            const int die = random.Range(1, 7);
            intInRange &= die >= 1 && die <= 6;
            ++buckets[static_cast<std::size_t>(die - 1)];
        }
        const bool meanIsHalf = IsNearlyEqual(sum / Samples, 0.5f, 0.02f);
        bool bucketsEven = true;
        for (const int count : buckets)
        {
            bucketsEven &= count > Samples / 6 - Samples / 40 && count < Samples / 6 + Samples / 40;
        }
        const bool emptyRangeIsMinimum = random.Range(5, 5) == 5 && random.Range(9, 3) == 9;
        const bool rangedFloat = [&] {
            for (int i = 0; i < 1000; ++i)
            {
                const float value = random.Range(-2.0f, 3.0f);
                if (value < -2.0f || value >= 3.0f) return false;
            }
            return true;
        }();

        bool unitShapes = true;
        for (int i = 0; i < 500; ++i)
        {
            unitShapes &= random.InsideUnitCircle().GetLength() <= 1.0001f;
            unitShapes &= IsNearlyEqual(random.OnUnitCircle().GetLength(), 1.0f, 0.001f);
            unitShapes &= IsNearlyEqual(random.OnUnitSphere().GetLength(), 1.0f, 0.001f);
            unitShapes &= random.InsideUnitSphere().GetLength() <= 1.0001f;
        }

        Random reseeded(99);
        const std::uint64_t before = reseeded.NextUInt64();
        reseeded.Seed(99);
        const bool reseedRestarts = reseeded.NextUInt64() == before && reseeded.GetSeed() == 99;
        const bool globalExists = Random::Global().NextFloat() < 1.0f;

        return Expect(deterministic, "the same seed should produce the same sequence") &&
            Expect(differentSeedsDiffer, "different seeds should differ") &&
            Expect(floatInRange, "NextFloat should stay in [0, 1)") &&
            Expect(intInRange, "an integer range should stay inside its bounds") &&
            Expect(meanIsHalf, "floats should average about one half") &&
            Expect(bucketsEven, "a six-sided die should land evenly") &&
            Expect(emptyRangeIsMinimum, "an empty integer range should return its minimum") &&
            Expect(rangedFloat, "a float range should stay inside its bounds") &&
            Expect(unitShapes, "unit circle and sphere samples should respect their radius") &&
            Expect(reseedRestarts, "reseeding should restart the sequence") &&
            Expect(globalExists, "the global generator should be usable");
    }

    bool RunUtilityTests()
    {
        const bool angles = IsNearlyEqual(ToRadians(180.0f), Pi) && IsNearlyEqual(ToDegrees(Pi), 180.0f);
        const bool wrap = IsNearlyEqual(WrapDegrees(370.0f), 10.0f) && IsNearlyEqual(WrapDegrees(-190.0f), 170.0f) &&
            IsNearlyEqual(WrapDegrees(180.0f), 180.0f);
        const bool lerp = IsNearlyEqual(Lerp(2.0f, 4.0f, 0.25f), 2.5f) && Clamp01(1.5f) == 1.0f && Clamp01(-1.0f) == 0.0f;
        return Expect(angles, "degree and radian conversion should agree with pi") &&
            Expect(wrap, "WrapDegrees should fold into (-180, 180]") &&
            Expect(lerp, "Lerp and Clamp01 should behave");
    }
}

bool RunMathTests()
{
    return Expect(RunUtilityTests(), "math utility tests should pass") &&
        Expect(RunVectorTests(), "vector tests should pass") &&
        Expect(RunQuaternionTests(), "quaternion tests should pass") &&
        Expect(RunMatrixDecompositionTests(), "matrix decomposition tests should pass") &&
        Expect(RunRandomTests(), "random tests should pass");
}

bool RunMatrixTests()
{
    using GameEngine::Math::Matrix4x4;
    using GameEngine::Math::Vector3;

    const Matrix4x4 localToWorld =
        Matrix4x4::CreateScale({ 2.0f, 3.0f, 4.0f }) *
        Matrix4x4::CreateTranslation({ 5.0f, 6.0f, 7.0f });
    const Vector3 worldPoint = localToWorld.TransformPoint({ 1.0f, 1.0f, 1.0f });
    Matrix4x4 worldToLocal;
    const bool inverted = localToWorld.TryInvert(worldToLocal);
    const Vector3 restoredPoint = worldToLocal.TransformPoint(worldPoint);

    // Operations the FBX importer needs.
    const Matrix4x4 rotateZ = Matrix4x4::CreateRotationZDegrees(90.0f);
    const Vector3 rotatedX = rotateZ.TransformPoint({ 1.0f, 0.0f, 0.0f });
    const bool rotatesAboutZ =
        std::abs(rotatedX.GetX()) < 0.0001f && std::abs(rotatedX.GetY() - 1.0f) < 0.0001f;

    // A rotation is orthonormal, so transposing it inverts it.
    Matrix4x4 rotateZInverse;
    const bool transposeInvertsRotation = rotateZ.TryInvert(rotateZInverse) &&
        std::abs(rotateZ.Transpose().GetElement(0, 1) - rotateZInverse.GetElement(0, 1)) < 0.0001f &&
        std::abs(rotateZ.Transpose().GetElement(1, 0) - rotateZInverse.GetElement(1, 0)) < 0.0001f;

    // TransformDirection ignores translation, unlike TransformPoint.
    const Matrix4x4 translated = Matrix4x4::CreateTranslation({ 10.0f, 20.0f, 30.0f });
    const Vector3 direction = translated.TransformDirection({ 1.0f, 0.0f, 0.0f });
    const bool ignoresTranslation = std::abs(direction.GetX() - 1.0f) < 0.0001f &&
        std::abs(direction.GetY()) < 0.0001f && std::abs(direction.GetZ()) < 0.0001f;

    // A negative determinant means the transform mirrors, which flips triangle winding.
    const bool detectsMirroring =
        std::abs(Matrix4x4::Identity().GetDeterminant() - 1.0f) < 0.0001f &&
        Matrix4x4::CreateScale({ 1.0f, 1.0f, -1.0f }).GetDeterminant() < 0.0f &&
        std::abs(Matrix4x4::CreateScale({ 2.0f, 3.0f, 4.0f }).GetDeterminant() - 24.0f) < 0.0001f;

    const Vector3 normalized = Vector3(0.0f, 3.0f, 4.0f).Normalized();
    const bool normalizes = std::abs(normalized.GetLength() - 1.0f) < 0.0001f &&
        std::abs(normalized.GetZ() - 0.8f) < 0.0001f &&
        Vector3().Normalized().GetLength() == 0.0f;

    return Expect(
               rotatesAboutZ && transposeInvertsRotation,
               "single-axis rotation should follow the engine's row-vector convention") &&
        Expect(ignoresTranslation, "TransformDirection should ignore translation") &&
        Expect(detectsMirroring, "the determinant should report scale and mirroring") &&
        Expect(normalizes, "Vector3 normalization should produce unit length and tolerate zero") &&
        Expect(
               std::abs(worldPoint.GetX() - 7.0f) < 0.0001f &&
               std::abs(worldPoint.GetY() - 9.0f) < 0.0001f &&
               std::abs(worldPoint.GetZ() - 11.0f) < 0.0001f,
               "matrix multiplication should preserve the engine row-vector convention") &&
        Expect(inverted, "a scale and translation matrix should be invertible") &&
        Expect(
            std::abs(restoredPoint.GetX() - 1.0f) < 0.0001f &&
            std::abs(restoredPoint.GetY() - 1.0f) < 0.0001f &&
            std::abs(restoredPoint.GetZ() - 1.0f) < 0.0001f,
            "an inverse matrix should restore transformed points");
}

bool RunViewportProjectionTests()
{
    using namespace GameEngine::Math;

    // 원점을 바라보고 z = -10에 선 카메라. 왼손 좌표계라 +z가 앞이므로 뒤로 물러선 자리다.
    const Matrix4x4 view = Matrix4x4::CreateTranslation({ 0.0f, 0.0f, 10.0f });
    const Matrix4x4 projection =
        Matrix4x4::CreatePerspectiveFieldOfViewLeftHanded(60.0f, 2.0f, 0.1f, 1000.0f);
    const Matrix4x4 viewProjection = view * projection;
    constexpr float Width = 800.0f;
    constexpr float Height = 400.0f;

    // 카메라가 정면으로 보는 원점은 화면 한가운데에 온다.
    const ViewportPoint center = ProjectToViewport({ 0.0f, 0.0f, 0.0f }, viewProjection, Width, Height);
    const bool centerProjects = center.isInFront &&
        std::abs(center.x - Width * 0.5f) < 0.001f && std::abs(center.y - Height * 0.5f) < 0.001f;

    // +x는 화면 오른쪽으로, +y는 화면 위쪽 — 픽셀 좌표에서는 작은 y — 로 간다.
    const ViewportPoint right = ProjectToViewport({ 1.0f, 0.0f, 0.0f }, viewProjection, Width, Height);
    const ViewportPoint up = ProjectToViewport({ 0.0f, 1.0f, 0.0f }, viewProjection, Width, Height);
    const bool axesPointAsExpected = right.isInFront && up.isInFront &&
        right.x > center.x && std::abs(right.y - center.y) < 0.001f &&
        up.y < center.y && std::abs(up.x - center.x) < 0.001f;

    // 카메라 뒤의 점은 픽셀을 말하지 않는다. 원근 분할만으로는 앞뒤가 구별되지 않기 때문이다.
    const ViewportPoint behind =
        ProjectToViewport({ 0.0f, 0.0f, -20.0f }, viewProjection, Width, Height);
    const bool behindRejected = !behind.isInFront;

    // x축을 끌어 본다: 월드 1단위가 화면에서 (right.x - center.x)픽셀이므로, 그만큼 끌면 정확히
    // 1단위가 나와야 한다.
    const float pixelsPerUnit = right.x - center.x;
    const float alongX = ComputeAxisDragDistance(
        { 0.0f, 0.0f, 0.0f }, Vector3::Right, viewProjection, Width, Height, pixelsPerUnit, 0.0f);
    const bool dragMatchesProjection = std::abs(alongX - 1.0f) < 0.001f;

    // 축과 직교하는 방향으로만 끌면 그 축은 움직이지 않는다.
    const float acrossX = ComputeAxisDragDistance(
        { 0.0f, 0.0f, 0.0f }, Vector3::Right, viewProjection, Width, Height, 0.0f, 50.0f);
    const bool perpendicularDragIsIdle = std::abs(acrossX) < 0.001f;

    // 시선과 나란한 축은 화면에서 점이라 어떤 픽셀 이동도 거리를 뜻하지 못한다.
    const float alongView = ComputeAxisDragDistance(
        { 0.0f, 0.0f, 0.0f }, Vector3::Forward, viewProjection, Width, Height, 40.0f, 40.0f);
    const bool edgeOnAxisIsIdle = std::abs(alongView) < 0.001f;

    const bool snapRounds = std::abs(SnapToGrid(1.2f, 0.5f) - 1.0f) < 0.0001f &&
        std::abs(SnapToGrid(1.3f, 0.5f) - 1.5f) < 0.0001f &&
        std::abs(SnapToGrid(-1.2f, 0.5f) + 1.0f) < 0.0001f;
    const bool snapIgnoresNonPositiveSpacing =
        std::abs(SnapToGrid(1.23f, 0.0f) - 1.23f) < 0.0001f &&
        std::abs(SnapToGrid(1.23f, -1.0f) - 1.23f) < 0.0001f;

    return Expect(centerProjects, "a point the camera faces should land in the viewport center") &&
        Expect(axesPointAsExpected, "+x should go right and +y should go up the screen") &&
        Expect(behindRejected, "a point behind the camera should not report pixels") &&
        Expect(
            dragMatchesProjection,
            "dragging an axis by its projected length should move exactly one unit") &&
        Expect(perpendicularDragIsIdle, "dragging across an axis should not move along it") &&
        Expect(edgeOnAxisIsIdle, "an axis seen edge-on should refuse to convert a drag") &&
        Expect(snapRounds, "snapping should round to the nearest grid point") &&
        Expect(snapIgnoresNonPositiveSpacing, "a non-positive spacing should leave the value alone");
}

static const TestSupport::Registration gMathTests{
    "Math", "math tests should pass", RunMathTests };

static const TestSupport::Registration gMatrixTests{
    "Math", "matrix tests should pass", RunMatrixTests };

static const TestSupport::Registration gViewportProjectionTests{
    "Math", "viewport projection tests should pass", RunViewportProjectionTests };
