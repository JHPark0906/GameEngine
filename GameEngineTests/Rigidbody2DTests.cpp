#include "Rigidbody2DTests.h"

#include <memory>

#include "Core/Aabb2D.h"
#include "Math/MathUtility.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Runtime/BoxCollider2D.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/Physics2DSystem.h"
#include "Runtime/Rigidbody2D.h"
#include "Runtime/Rigidbody3D.h"
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

    /// <summary>고체 하나와 몸체 하나를 만드는 데 반복되는 장면의 뼈대다.</summary>
    struct PhysicsFixture
    {
        GameEngine::Runtime::ObjectRegistry registry;
        GameEngine::Runtime::Input input;
        GameEngine::Runtime::RuntimeContext context{ registry, input };
        GameEngine::Runtime::SceneManager sceneManager{ context };
        std::unique_ptr<GameEngine::Runtime::Scene> scene =
            std::make_unique<GameEngine::Runtime::Scene>(context, "Rigidbody2D");

        [[nodiscard]] unsigned int AddScene()
        {
            return sceneManager.AddScene(std::move(scene));
        }
    };

    [[nodiscard]] bool IsNearly(const float actual, const float expected)
    {
        return GameEngine::Math::IsNearlyEqual(actual, expected, 0.0001f);
    }
}

bool RunRigidbody2DTests()
{
    using GameEngine::Core::Aabb2D;
    using GameEngine::Runtime::BoxCollider2D;
    using GameEngine::Runtime::Game;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::Physics2DSystem;
    using GameEngine::Runtime::Rigidbody2D;
    using GameEngine::Runtime::Rigidbody2DContact;
    using GameEngine::Runtime::Rigidbody3D;
    using GameEngine::Runtime::TilemapCollider2D;
    using GameEngine::Runtime::TilemapRenderer;

    // sweep 수학은 물리 장면 없이도 고속 이동·접촉 0을 고정할 수 있다.
    const Aabb2D moving{ { 0.0f, 0.0f }, { 1.0f, 1.0f } };
    const Aabb2D wall{ { 4.0f, 0.0f }, { 5.0f, 1.0f } };
    const auto fastHit = moving.SweepAgainst(wall, { 10.0f, 0.0f });
    const auto leavesWall = moving.SweepAgainst(wall, { -10.0f, 0.0f });
    const auto standingStill = moving.SweepAgainst(wall, { 0.0f, 0.0f });
    const Aabb2D floorBox{ { 0.0f, -0.5f }, { 1.0f, 0.5f } };
    const Aabb2D restingBox{ { 0.0f, 0.5f }, { 1.0f, 1.5f } };
    const auto entersTouchingFloor = restingBox.SweepAgainst(floorBox, { 0.0f, -1.0f });
    const auto leavesTouchingFloor = restingBox.SweepAgainst(floorBox, { 0.0f, 1.0f });
    const bool sweepFindsFirstContact = fastHit && IsNearly(fastHit->fraction, 0.3f) &&
        fastHit->normal == GameEngine::Math::Vector2{ -1.0f, 0.0f } &&
        !leavesWall && !standingStill && entersTouchingFloor &&
        IsNearly(entersTouchingFloor->fraction, 0.0f) && !leavesTouchingFloor;

    // Game의 한 프레임이 Scene Update 뒤에 실제 Simulate를 호출하는지도 확인한다. 시스템을
    // 직접 부르는 시험만 있으면, 이 연결이 빠져도 물리 단위 시험은 모두 통과할 수 있다.
    Game game{ nullptr, nullptr };
    auto gameScene = std::make_unique<GameEngine::Runtime::Scene>(
        game.GetRuntimeContext(), "GamePhysics");
    GameObject* const gameBodyObject = gameScene->CreateGameObject("GameBody");
    gameBodyObject->GetTransform().SetPosition({ 0.0f, 2.0f, 0.0f });
    Rigidbody2D* const gameBody = gameBodyObject->AddComponent<Rigidbody2D>();
    const unsigned int gameSceneId = game.AddScene(std::move(gameScene));
    game.GetPhysics2DSystem().SetGravity({ 0.0f, -10.0f });
    game.Update(Physics2DSystem::GetFixedDeltaTime());
    const bool gameRunsPhysics = gameSceneId != 0 && gameBody &&
        gameBodyObject->GetTransform().GetWorldPosition().GetY() < 2.0f &&
        gameBody->GetVelocity().GetY() < 0.0f;

    PhysicsFixture floorFixture;
    GameObject* const floor = floorFixture.scene->CreateGameObject("Floor");
    BoxCollider2D* const floorCollider = floor->AddComponent<BoxCollider2D>();
    floorCollider->SetSize({ 12.0f, 1.0f });

    GameObject* const falling = floorFixture.scene->CreateGameObject("Falling");
    falling->GetTransform().SetPosition({ 0.0f, 3.0f, 0.0f });
    BoxCollider2D* const fallingCollider = falling->AddComponent<BoxCollider2D>();
    Rigidbody2D* const fallingBody = falling->AddComponent<Rigidbody2D>();
    const unsigned int floorSceneId = floorFixture.AddScene();
    Physics2DSystem floorPhysics;
    floorPhysics.SetGravity({ 0.0f, -10.0f });
    for (int step = 0; step < 120; ++step)
    {
        floorPhysics.Simulate(floorFixture.sceneManager, Physics2DSystem::GetFixedDeltaTime());
    }
    const bool fallsAndRests = floorSceneId != 0 && floorCollider && fallingCollider && fallingBody &&
        IsNearly(falling->GetTransform().GetWorldPosition().GetY(), 1.0f) &&
        IsNearly(fallingBody->GetVelocity().GetY(), 0.0f) && fallingBody->IsGrounded() &&
        fallingBody->HasContact(Rigidbody2DContact::Below) &&
        fallingCollider->GetOverlapping().empty();

    // 바닥에 닿은 순간 위로 향하는 속도는 분리 이동이다. sweep이 경계의 fraction 0을 무조건
    // 충돌로 보면 점프 속도까지 0이 되어 바닥을 떠날 수 없다.
    fallingBody->SetVelocity({ 0.0f, 5.0f });
    floorPhysics.Simulate(floorFixture.sceneManager, Physics2DSystem::GetFixedDeltaTime());
    const bool canJumpAway = falling->GetTransform().GetWorldPosition().GetY() > 1.0f &&
        fallingBody->GetVelocity().GetY() > 0.0f && !fallingBody->IsGrounded();

    // 한 fixed step 안에서 벽 하나보다 훨씬 멀리 가도 sweep은 통과를 허용하지 않는다. 중력은
    // 끄되, 충돌한 X 성분만 0이 되는지를 함께 본다.
    PhysicsFixture wallFixture;
    GameObject* const wallObject = wallFixture.scene->CreateGameObject("Wall");
    wallObject->GetTransform().SetPosition({ 0.0f, 0.0f, 0.0f });
    BoxCollider2D* const wallCollider = wallObject->AddComponent<BoxCollider2D>();
    wallCollider->SetSize({ 1.0f, 8.0f });

    GameObject* const runner = wallFixture.scene->CreateGameObject("Runner");
    runner->GetTransform().SetPosition({ -5.0f, 0.0f, 0.0f });
    BoxCollider2D* const runnerCollider = runner->AddComponent<BoxCollider2D>();
    Rigidbody2D* const runnerBody = runner->AddComponent<Rigidbody2D>();
    runnerBody->SetGravityScale(0.0f);
    runnerBody->SetVelocity({ 600.0f, 2.0f });
    const unsigned int wallSceneId = wallFixture.AddScene();
    Physics2DSystem wallPhysics;
    wallPhysics.SetGravity({ 0.0f, 0.0f });
    wallPhysics.Simulate(wallFixture.sceneManager, Physics2DSystem::GetFixedDeltaTime());
    const bool stopsAtWallAndSlides = wallSceneId != 0 && wallCollider && runnerCollider && runnerBody &&
        IsNearly(runner->GetTransform().GetWorldPosition().GetX(), -1.0f) &&
        IsNearly(runnerBody->GetVelocity().GetX(), 0.0f) &&
        runner->GetTransform().GetWorldPosition().GetY() > 0.0f &&
        runnerBody->HasContact(Rigidbody2DContact::Right);

    // Trigger는 겹침 목록에는 남지만 고체 sweep에서는 건너뛴다.
    PhysicsFixture triggerFixture;
    GameObject* const sensor = triggerFixture.scene->CreateGameObject("Sensor");
    sensor->GetTransform().SetPosition({ 1.0f, 0.0f, 0.0f });
    BoxCollider2D* const sensorCollider = sensor->AddComponent<BoxCollider2D>();
    sensorCollider->SetTrigger(true);

    GameObject* const visitor = triggerFixture.scene->CreateGameObject("Visitor");
    BoxCollider2D* const visitorCollider = visitor->AddComponent<BoxCollider2D>();
    Rigidbody2D* const visitorBody = visitor->AddComponent<Rigidbody2D>();
    visitorBody->SetGravityScale(0.0f);
    visitorBody->SetVelocity({ 60.0f, 0.0f });
    const unsigned int triggerSceneId = triggerFixture.AddScene();
    Physics2DSystem triggerPhysics;
    triggerPhysics.SetGravity({ 0.0f, 0.0f });
    triggerPhysics.Simulate(triggerFixture.sceneManager, Physics2DSystem::GetFixedDeltaTime());
    const bool triggerDoesNotBlock = triggerSceneId != 0 && sensorCollider && visitorCollider &&
        visitorBody && IsNearly(visitor->GetTransform().GetWorldPosition().GetX(), 1.0f) &&
        IsNearly(visitorBody->GetVelocity().GetX(), 60.0f) &&
        visitorCollider->IsOverlapping(sensorCollider->GetInstanceId());

    // 활성 몸체끼리의 반발은 아직 없다. 순회 중 먼저 움직인 몸체가 뒤의 몸체를 정적으로
    // 취급하는 식의 반쪽 해소가 되지 않고, 둘 다 같은 속도로 서로를 지난다.
    PhysicsFixture bodiesFixture;
    GameObject* const leftBodyObject = bodiesFixture.scene->CreateGameObject("LeftBody");
    leftBodyObject->GetTransform().SetPosition({ -2.0f, 0.0f, 0.0f });
    BoxCollider2D* const leftBodyCollider = leftBodyObject->AddComponent<BoxCollider2D>();
    Rigidbody2D* const leftBody = leftBodyObject->AddComponent<Rigidbody2D>();
    leftBody->SetGravityScale(0.0f);
    leftBody->SetVelocity({ 120.0f, 0.0f });

    GameObject* const rightBodyObject = bodiesFixture.scene->CreateGameObject("RightBody");
    rightBodyObject->GetTransform().SetPosition({ 2.0f, 0.0f, 0.0f });
    BoxCollider2D* const rightBodyCollider = rightBodyObject->AddComponent<BoxCollider2D>();
    Rigidbody2D* const rightBody = rightBodyObject->AddComponent<Rigidbody2D>();
    rightBody->SetGravityScale(0.0f);
    rightBody->SetVelocity({ -120.0f, 0.0f });
    const unsigned int bodiesSceneId = bodiesFixture.AddScene();
    Physics2DSystem bodiesPhysics;
    bodiesPhysics.SetGravity({ 0.0f, -10.0f });
    bodiesPhysics.Simulate(bodiesFixture.sceneManager, Physics2DSystem::GetFixedDeltaTime());
    bodiesPhysics.Simulate(bodiesFixture.sceneManager, Physics2DSystem::GetFixedDeltaTime());
    const bool activeBodiesPassThrough = bodiesSceneId != 0 && leftBodyCollider && leftBody &&
        rightBodyCollider && rightBody &&
        IsNearly(leftBodyObject->GetTransform().GetWorldPosition().GetX(), 2.0f) &&
        IsNearly(rightBodyObject->GetTransform().GetWorldPosition().GetX(), -2.0f) &&
        IsNearly(leftBody->GetVelocity().GetX(), 120.0f) &&
        IsNearly(rightBody->GetVelocity().GetX(), -120.0f) &&
        IsNearly(leftBodyObject->GetTransform().GetWorldPosition().GetY(), 0.0f) &&
        IsNearly(rightBodyObject->GetTransform().GetWorldPosition().GetY(), 0.0f);

    // 다른 차원의 활성 몸체가 가진 2D 콜라이더도 정적 벽이 될 수는 없다. 그러면 2D와 3D의
    // 호출 순서가 충돌 결과를 정하게 되므로, 고체 충돌에서는 동적 몸체처럼 함께 건너뛴다.
    PhysicsFixture otherDimensionFixture;
    GameObject* const otherDimensionTarget = otherDimensionFixture.scene->CreateGameObject("OtherDimension");
    BoxCollider2D* const otherDimensionCollider = otherDimensionTarget->AddComponent<BoxCollider2D>();
    Rigidbody3D* const otherDimensionBody = otherDimensionTarget->AddComponent<Rigidbody3D>();
    otherDimensionBody->SetGravityScale(0.0f);

    GameObject* const otherDimensionRunner = otherDimensionFixture.scene->CreateGameObject("OtherDimensionRunner");
    otherDimensionRunner->GetTransform().SetPosition({ -5.0f, 0.0f, 0.0f });
    BoxCollider2D* const otherDimensionRunnerCollider =
        otherDimensionRunner->AddComponent<BoxCollider2D>();
    Rigidbody2D* const otherDimensionRunnerBody = otherDimensionRunner->AddComponent<Rigidbody2D>();
    otherDimensionRunnerBody->SetGravityScale(0.0f);
    otherDimensionRunnerBody->SetVelocity({ 600.0f, 0.0f });
    const unsigned int otherDimensionSceneId = otherDimensionFixture.AddScene();
    Physics2DSystem otherDimensionPhysics;
    otherDimensionPhysics.SetGravity({ 0.0f, 0.0f });
    otherDimensionPhysics.Simulate(
        otherDimensionFixture.sceneManager, Physics2DSystem::GetFixedDeltaTime());
    const bool otherDimensionBodyDoesNotBlock = otherDimensionSceneId != 0 &&
        otherDimensionCollider && otherDimensionBody && otherDimensionRunnerCollider &&
        otherDimensionRunnerBody &&
        IsNearly(otherDimensionRunner->GetTransform().GetWorldPosition().GetX(), 5.0f) &&
        IsNearly(otherDimensionRunnerBody->GetVelocity().GetX(), 600.0f);

    // 타일맵의 넓은 AABB가 아니라 채워진 칸 하나만 막아야 한다. (0,0)은 고체, (1,0)은 빈 칸이다.
    PhysicsFixture tileFixture;
    GameObject* const map = tileFixture.scene->CreateGameObject("Map");
    TilemapRenderer* const tilemap = map->AddComponent<TilemapRenderer>();
    tilemap->SetColumns(2);
    tilemap->SetRows(1);
    tilemap->SetCellSize({ 1.0f, 1.0f });
    static_cast<void>(tilemap->SetTile(0, 0, 1));
    TilemapCollider2D* const tileCollider = map->AddComponent<TilemapCollider2D>();

    GameObject* const tileBodyObject = tileFixture.scene->CreateGameObject("TileBody");
    tileBodyObject->GetTransform().SetPosition({ 0.5f, 3.0f, 0.0f });
    BoxCollider2D* const tileBodyCollider = tileBodyObject->AddComponent<BoxCollider2D>();
    Rigidbody2D* const tileBody = tileBodyObject->AddComponent<Rigidbody2D>();

    GameObject* const emptyTileBodyObject = tileFixture.scene->CreateGameObject("EmptyTileBody");
    emptyTileBodyObject->GetTransform().SetPosition({ 1.5f, 3.0f, 0.0f });
    BoxCollider2D* const emptyTileBodyCollider = emptyTileBodyObject->AddComponent<BoxCollider2D>();
    Rigidbody2D* const emptyTileBody = emptyTileBodyObject->AddComponent<Rigidbody2D>();
    const unsigned int tileSceneId = tileFixture.AddScene();
    Physics2DSystem tilePhysics;
    tilePhysics.SetGravity({ 0.0f, -10.0f });
    for (int step = 0; step < 120; ++step)
    {
        tilePhysics.Simulate(tileFixture.sceneManager, Physics2DSystem::GetFixedDeltaTime());
    }
    const bool tilemapUsesOnlyFilledCells =
        tileSceneId != 0 && tileCollider && tileBodyCollider && tileBody && emptyTileBodyCollider &&
        emptyTileBody &&
        IsNearly(tileBodyObject->GetTransform().GetWorldPosition().GetY(), 1.5f) &&
        tileBody->IsGrounded() &&
        emptyTileBodyObject->GetTransform().GetWorldPosition().GetY() < 0.5f &&
        !emptyTileBody->IsGrounded();

    // 혼합 몸체가 된 순간에는 마지막 fixed step의 접촉도 더 이상 유효하지 않다. 다음 2D fixed
    // step에서 이를 비워, 이미 바닥을 떠난 설정이 grounded로 남지 않게 한다.
    PhysicsFixture mixedContactFixture;
    GameObject* const contactFloor = mixedContactFixture.scene->CreateGameObject("ContactFloor");
    BoxCollider2D* const contactFloorCollider = contactFloor->AddComponent<BoxCollider2D>();
    contactFloorCollider->SetSize({ 12.0f, 1.0f });
    GameObject* const contactBodyObject = mixedContactFixture.scene->CreateGameObject("ContactBody");
    contactBodyObject->GetTransform().SetPosition({ 0.0f, 1.0f, 0.0f });
    BoxCollider2D* const contactBodyCollider = contactBodyObject->AddComponent<BoxCollider2D>();
    Rigidbody2D* const contactBody = contactBodyObject->AddComponent<Rigidbody2D>();
    contactBody->SetGravityScale(0.0f);
    contactBody->SetVelocity({ 0.0f, -1.0f });
    const unsigned int mixedContactSceneId = mixedContactFixture.AddScene();
    Physics2DSystem mixedContactPhysics;
    mixedContactPhysics.SetGravity({ 0.0f, 0.0f });
    mixedContactPhysics.Simulate(mixedContactFixture.sceneManager, Physics2DSystem::GetFixedDeltaTime());
    const bool contactWasGrounded = contactBody->IsGrounded();
    Rigidbody3D* const contactConflict = contactBodyObject->AddComponent<Rigidbody3D>();
    mixedContactPhysics.Simulate(mixedContactFixture.sceneManager, Physics2DSystem::GetFixedDeltaTime());
    const bool mixedDimensionsClearContacts = mixedContactSceneId != 0 && contactFloorCollider &&
        contactBodyCollider && contactBody && contactConflict && contactWasGrounded &&
        !contactBody->IsGrounded() && contactBody->GetContacts() == Rigidbody2DContact::None;

    return Expect(sweepFindsFirstContact, "an AABB sweep should find high-speed first contact") &&
        Expect(gameRunsPhysics, "a game frame should run rigidbody physics after scene updates") &&
        Expect(fallsAndRests, "a rigidbody should fall onto a solid floor and report grounded") &&
        Expect(canJumpAway, "a rigidbody should be able to jump away from a touching floor") &&
        Expect(stopsAtWallAndSlides, "a rigidbody should stop on a wall without losing free-axis motion") &&
        Expect(triggerDoesNotBlock, "a trigger should report overlap without blocking a rigidbody") &&
        Expect(activeBodiesPassThrough, "active rigidbodies should pass through one another") &&
        Expect(otherDimensionBodyDoesNotBlock, "a 3D body should not make a 2D collider a static wall") &&
        Expect(tilemapUsesOnlyFilledCells, "a tilemap should stop bodies only on filled cells") &&
        Expect(mixedDimensionsClearContacts, "mixed 2D and 3D rigidbodies should clear stale contacts");
}

static const TestSupport::Registration gRigidbody2DTests{
    "Physics", "rigidbody 2D tests should pass", RunRigidbody2DTests };
