#include "PhysicsTests.h"

#include <cstddef>
#include <iostream>
#include <memory>

#include "Core/Aabb2D.h"
#include "Runtime/BoxCollider2D.h"
#include "Runtime/Collider2D.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/Physics2DSystem.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/TilemapCollider2D.h"
#include "Runtime/TilemapRenderer.h"
#include "Runtime/Transform.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>겹침 목록에 그 id가 정확히 한 번 들어 있는지다.</summary>
    [[nodiscard]] bool ListsOnly(
        const std::vector<unsigned int>& ids, const unsigned int expected)
    {
        return ids.size() == 1 && ids[0] == expected;
    }
}

bool RunAabb2DTests()
{
    using GameEngine::Core::Aabb2D;

    constexpr Aabb2D unit = Aabb2D::FromCenterSize({ 0.0f, 0.0f }, { 2.0f, 2.0f });
    const bool centeredCorners = unit.min == GameEngine::Math::Vector2{ -1.0f, -1.0f } &&
        unit.max == GameEngine::Math::Vector2{ 1.0f, 1.0f } &&
        unit.GetCenter() == GameEngine::Math::Vector2{ 0.0f, 0.0f } &&
        unit.GetSize() == GameEngine::Math::Vector2{ 2.0f, 2.0f };

    // 모서리 순서를 묻지 않는다: 어느 쪽을 먼저 주든 같은 영역이다.
    const bool pointsAreOrdered =
        Aabb2D::FromPoints({ 3.0f, 4.0f }, { 1.0f, 2.0f }) ==
        Aabb2D{ { 1.0f, 2.0f }, { 3.0f, 4.0f } };

    const bool overlapsWhenSharingArea =
        unit.Overlaps(Aabb2D::FromCenterSize({ 1.5f, 0.0f }, { 2.0f, 2.0f }));
    // 맞닿기만 한 것은 겹친 것이 아니다. 나란한 타일 두 칸이 서로 겹쳤다고 답하면 격자 전체가
    // 충돌 상태가 된다.
    const bool touchingIsNotOverlap =
        !unit.Overlaps(Aabb2D{ { 1.0f, -1.0f }, { 3.0f, 1.0f } });
    const bool separatedDoesNotOverlap =
        !unit.Overlaps(Aabb2D{ { 2.0f, 2.0f }, { 3.0f, 3.0f } });

    constexpr Aabb2D empty{};
    const bool emptyOverlapsNothing = !empty.Overlaps(unit) && !unit.Overlaps(empty) &&
        empty.IsEmpty() && !unit.IsEmpty() &&
        Aabb2D{ { 0.0f, 0.0f }, { 0.0f, 5.0f } }.IsEmpty();

    const bool containsIsHalfOpen = unit.Contains({ -1.0f, -1.0f }) &&
        unit.Contains({ 0.0f, 0.0f }) && !unit.Contains({ 1.0f, 0.0f }) &&
        !unit.Contains({ 0.0f, 1.0f });

    const bool unionCoversBoth =
        unit.UnitedWith(Aabb2D{ { 2.0f, 2.0f }, { 3.0f, 4.0f } }) ==
            Aabb2D{ { -1.0f, -1.0f }, { 3.0f, 4.0f } } &&
        unit.UnitedWith(empty) == unit && empty.UnitedWith(unit) == unit;

    return Expect(centeredCorners, "a box from a centre and size should span half each way") &&
        Expect(pointsAreOrdered, "a box from two points should not care which came first") &&
        Expect(overlapsWhenSharingArea, "boxes sharing area should overlap") &&
        Expect(touchingIsNotOverlap, "boxes that only touch should not overlap") &&
        Expect(separatedDoesNotOverlap, "separated boxes should not overlap") &&
        Expect(emptyOverlapsNothing, "an empty box should overlap nothing") &&
        Expect(containsIsHalfOpen, "containment should include the low corner and exclude the high") &&
        Expect(unionCoversBoth, "a union should cover both boxes and ignore empty ones");
}

bool RunCollider2DTests()
{
    using GameEngine::Runtime::BoxCollider2D;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::Physics2DSystem;
    using GameEngine::Runtime::Scene;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "Colliders");
    const auto addBox = [&scene](const char* const name, const float x)
    {
        GameObject* const object = scene->CreateGameObject(name);
        object->GetTransform().SetPosition({ x, 0.0f, 0.0f });
        return object->AddComponent<BoxCollider2D>();
    };
    // 한 변이 1인 사각형 둘. 0.5만큼 떨어져 있으니 겹친다.
    BoxCollider2D* const left = addBox("Left", 0.0f);
    BoxCollider2D* const right = addBox("Right", 0.5f);
    GameObject* const rightObject = right ? right->GetGameObject() : nullptr;

    const unsigned int sceneId = sceneManager.AddScene(std::move(scene));
    if (!Expect(sceneId != 0 && left && right && rightObject, "the collider scene should assemble"))
    {
        return false;
    }

    Physics2DSystem physics;
    const std::size_t firstPairs = physics.Synchronize(sceneManager);
    const bool overlapReported = firstPairs == 1 &&
        ListsOnly(left->GetOverlapping(), right->GetInstanceId()) &&
        ListsOnly(right->GetOverlapping(), left->GetInstanceId());
    // 들어옴은 처음 겹친 프레임에만이다.
    const bool enteredOnce = ListsOnly(left->GetEntered(), right->GetInstanceId()) &&
        left->GetExited().empty();

    // 머무는 프레임은 목록만 있고 표시는 없다.
    static_cast<void>(physics.Synchronize(sceneManager));
    const bool staysQuiet = ListsOnly(left->GetOverlapping(), right->GetInstanceId()) &&
        left->GetEntered().empty() && left->GetExited().empty();

    // 떼어 놓으면 그 프레임에 나감이 한 번 나오고 목록이 빈다.
    rightObject->GetTransform().SetPosition({ 5.0f, 0.0f, 0.0f });
    const std::size_t separatedPairs = physics.Synchronize(sceneManager);
    const bool exitedOnce = separatedPairs == 0 && left->GetOverlapping().empty() &&
        ListsOnly(left->GetExited(), right->GetInstanceId()) && left->GetEntered().empty();

    // 크기가 없는 콜라이더는 어디에 있든 겹치지 않는다.
    rightObject->GetTransform().SetPosition({ 0.0f, 0.0f, 0.0f });
    right->SetSize({ 0.0f, 1.0f });
    const bool zeroSizeNeverOverlaps = physics.Synchronize(sceneManager) == 0;
    right->SetSize({ 1.0f, 1.0f });

    // 꺼진 컴포넌트와 꺼진 오브젝트는 판정에서 빠진다.
    right->SetEnabled(false);
    const bool disabledComponentIsOut = physics.Synchronize(sceneManager) == 0;
    right->SetEnabled(true);
    rightObject->SetActive(false);
    const bool inactiveObjectIsOut = physics.Synchronize(sceneManager) == 0;
    rightObject->SetActive(true);
    const bool returnsWhenActive = physics.Synchronize(sceneManager) == 1;

    // offset은 오브젝트의 자리에서 사각형을 옮긴다. 둘 다 원점에 있어도 떨어뜨릴 수 있다.
    right->SetOffset({ 3.0f, 0.0f });
    const bool offsetMovesTheShape = physics.Synchronize(sceneManager) == 0;

    // isTrigger는 판정을 바꾸지 않는다. 이 엔진의 콜라이더는 어느 쪽이든 밀지 않으므로, 그
    // 표시는 겹침을 어떻게 쓸지에 대한 것이지 겹치는지에 대한 것이 아니다.
    right->SetOffset({ 0.0f, 0.0f });
    right->SetTrigger(true);
    const bool triggerStillOverlaps = physics.Synchronize(sceneManager) == 1 && right->IsTrigger();

    return Expect(overlapReported, "two overlapping boxes should each list the other") &&
        Expect(enteredOnce, "the first overlapping frame should report an enter") &&
        Expect(staysQuiet, "a continuing overlap should report neither enter nor exit") &&
        Expect(exitedOnce, "moving apart should report an exit and empty the list") &&
        Expect(zeroSizeNeverOverlaps, "a box with no width should never overlap") &&
        Expect(disabledComponentIsOut, "a disabled collider should leave the test") &&
        Expect(inactiveObjectIsOut, "a collider on an inactive object should leave the test") &&
        Expect(returnsWhenActive, "re-activating should bring the collider back") &&
        Expect(offsetMovesTheShape, "an offset should move the shape away from the object") &&
        Expect(triggerStillOverlaps, "a trigger should still be reported as overlapping");
}

bool RunTilemapCollider2DTests()
{
    using GameEngine::Runtime::BoxCollider2D;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::Physics2DSystem;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::TilemapCollider2D;
    using GameEngine::Runtime::TilemapRenderer;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "TilemapColliders");

    // 4x4 격자, 칸 하나가 1x1. 채워진 칸은 (0,0) 하나뿐이다.
    GameObject* const mapObject = scene->CreateGameObject("Map");
    TilemapRenderer* const tilemap = mapObject->AddComponent<TilemapRenderer>();
    tilemap->SetColumns(4);
    tilemap->SetRows(4);
    tilemap->SetCellSize({ 1.0f, 1.0f });
    static_cast<void>(tilemap->SetTile(0, 0, 7));
    TilemapCollider2D* const mapCollider = mapObject->AddComponent<TilemapCollider2D>();

    // 작은 사각형 하나를 옮겨 가며 어느 칸에서 겹치는지 본다.
    GameObject* const probeObject = scene->CreateGameObject("Probe");
    BoxCollider2D* const probe = probeObject->AddComponent<BoxCollider2D>();
    probe->SetSize({ 0.4f, 0.4f });

    const unsigned int sceneId = sceneManager.AddScene(std::move(scene));
    if (!Expect(
            sceneId != 0 && tilemap && mapCollider && probe,
            "the tilemap collider scene should assemble"))
    {
        return false;
    }

    Physics2DSystem physics;
    const auto overlapsAt = [&physics, &sceneManager, probeObject](const float x, const float y)
    {
        probeObject->GetTransform().SetPosition({ x, y, 0.0f });
        return physics.Synchronize(sceneManager) == 1;
    };

    // 격자를 품는 사각형은 0..4이므로, 아래 셋은 모두 그 안이다. 채워진 칸 위에서만 겹쳐야 한다.
    const bool overlapsFilledCell = overlapsAt(0.5f, 0.5f);
    const bool ignoresEmptyCell = !overlapsAt(2.5f, 2.5f);
    const bool ignoresEmptyCellBesideTheFilledOne = !overlapsAt(1.5f, 0.5f);
    const bool ignoresOutsideTheGrid = !overlapsAt(10.0f, 10.0f);

    // 칸을 칠하면 충돌도 함께 바뀐다. 콜라이더가 자기 사본을 두지 않는다는 것이 이 줄의 뜻이다.
    static_cast<void>(tilemap->SetTile(2, 2, 3));
    const bool followsThePaintedTile = overlapsAt(2.5f, 2.5f);
    static_cast<void>(tilemap->SetTile(2, 2, TilemapRenderer::EmptyTile));
    const bool followsTheErasedTile = !overlapsAt(2.5f, 2.5f);

    // 격자가 옮겨지면 채워진 칸도 함께 옮겨진다.
    mapObject->GetTransform().SetPosition({ 10.0f, 0.0f, 0.0f });
    const bool movesWithTheObject = overlapsAt(10.5f, 0.5f) && !overlapsAt(0.5f, 0.5f);

    // 격자 없이 붙은 콜라이더는 아무것도 아니다: 같은 오브젝트의 렌더러가 모양을 정의한다.
    GameObject* const lonelyObject =
        sceneManager.GetScene(sceneId)->CreateGameObject("NoTilemap");
    TilemapCollider2D* const lonely = lonelyObject->AddComponent<TilemapCollider2D>();
    const bool emptyWithoutRenderer = lonely && lonely->GetWorldBounds().IsEmpty() &&
        !lonely->OverlapsBox({ { -1.0f, -1.0f }, { 1.0f, 1.0f } });

    return Expect(overlapsFilledCell, "a probe over a filled cell should overlap the tilemap") &&
        Expect(ignoresEmptyCell, "a probe over an empty cell should not overlap") &&
        Expect(
            ignoresEmptyCellBesideTheFilledOne,
            "an empty cell next to a filled one should not overlap") &&
        Expect(ignoresOutsideTheGrid, "a probe outside the grid should not overlap") &&
        Expect(followsThePaintedTile, "painting a tile should make that cell collide") &&
        Expect(followsTheErasedTile, "erasing a tile should stop that cell colliding") &&
        Expect(movesWithTheObject, "moving the tilemap should move its collision") &&
        Expect(emptyWithoutRenderer, "a tilemap collider with no renderer should be empty");
}

static const TestSupport::Registration gAabb2DTests{
    "Physics", "axis-aligned box tests should pass", RunAabb2DTests };

static const TestSupport::Registration gCollider2DTests{
    "Physics", "collider tests should pass", RunCollider2DTests };

static const TestSupport::Registration gTilemapCollider2DTests{
    "Physics", "tilemap collider tests should pass", RunTilemapCollider2DTests };
