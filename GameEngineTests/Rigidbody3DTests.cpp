#include "Rigidbody3DTests.h"

#include <memory>

#include "Core/Aabb3D.h"
#include "Math/MathUtility.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Runtime/BoxCollider3D.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/Physics2DSystem.h"
#include "Runtime/Physics3DSystem.h"
#include "Runtime/Rigidbody2D.h"
#include "Runtime/Rigidbody3D.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
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
            std::make_unique<GameEngine::Runtime::Scene>(context, "Rigidbody3D");

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

bool RunRigidbody3DTests()
{
    using GameEngine::Core::Aabb3D;
    using GameEngine::Runtime::BoxCollider3D;
    using GameEngine::Runtime::Game;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::Physics2DSystem;
    using GameEngine::Runtime::Physics3DSystem;
    using GameEngine::Runtime::Rigidbody2D;
    using GameEngine::Runtime::Rigidbody3D;
    using GameEngine::Runtime::Rigidbody3DContact;

    // Aabb3D는 충돌 정책이 아니라 순수 경계 연산을 맡는다. 평면은 렌더링 경계로는 남지만,
    // 3D 물리의 양의 부피 겹침에는 참여하지 않는 규칙을 여기서 고정한다.
    const Aabb3D normalized = Aabb3D::FromPoints({ 2.0f, 1.0f, 3.0f }, { -2.0f, -1.0f, -3.0f });
    const Aabb3D touching{ { 2.0f, -1.0f, -3.0f }, { 3.0f, 1.0f, 3.0f } };
    const Aabb3D flat{ { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
    const bool aabbKeepsPhysicsVolumeRules = normalized.HasVolume() &&
        normalized.Contains({ -2.0f, -1.0f, -3.0f }) &&
        !normalized.Contains({ 2.0f, 1.0f, 3.0f }) && !normalized.Overlaps(touching) &&
        !flat.HasVolume() && !normalized.Overlaps(flat);

    // Game의 한 프레임이 Scene Update 뒤에 실제 3D Simulate를 호출하는지도 확인한다.
    Game game{ nullptr, nullptr };
    auto gameScene = std::make_unique<GameEngine::Runtime::Scene>(
        game.GetRuntimeContext(), "GamePhysics3D");
    GameObject* const gameBodyObject = gameScene->CreateGameObject("GameBody");
    gameBodyObject->GetTransform().SetPosition({ 0.0f, 2.0f, 0.0f });
    Rigidbody3D* const gameBody = gameBodyObject->AddComponent<Rigidbody3D>();
    const unsigned int gameSceneId = game.AddScene(std::move(gameScene));
    game.GetPhysics3DSystem().SetGravity({ 0.0f, -10.0f, 0.0f });
    game.Update(Physics3DSystem::GetFixedDeltaTime());
    const bool gameRunsPhysics = gameSceneId != 0 && gameBody &&
        gameBodyObject->GetTransform().GetWorldPosition().GetY() < 2.0f &&
        gameBody->GetVelocity().GetY() < 0.0f;

    PhysicsFixture floorFixture;
    GameObject* const floor = floorFixture.scene->CreateGameObject("Floor");
    BoxCollider3D* const floorCollider = floor->AddComponent<BoxCollider3D>();
    floorCollider->SetSize({ 12.0f, 1.0f, 12.0f });

    GameObject* const falling = floorFixture.scene->CreateGameObject("Falling");
    falling->GetTransform().SetPosition({ 0.0f, 3.0f, 0.0f });
    BoxCollider3D* const fallingCollider = falling->AddComponent<BoxCollider3D>();
    Rigidbody3D* const fallingBody = falling->AddComponent<Rigidbody3D>();
    const unsigned int floorSceneId = floorFixture.AddScene();
    Physics3DSystem floorPhysics;
    floorPhysics.SetGravity({ 0.0f, -10.0f, 0.0f });
    for (int step = 0; step < 120; ++step)
    {
        floorPhysics.Simulate(floorFixture.sceneManager, Physics3DSystem::GetFixedDeltaTime());
    }
    const bool fallsAndRests = floorSceneId != 0 && floorCollider && fallingCollider && fallingBody &&
        IsNearly(falling->GetTransform().GetWorldPosition().GetY(), 1.0f) &&
        IsNearly(fallingBody->GetVelocity().GetY(), 0.0f) && fallingBody->IsGrounded() &&
        fallingBody->HasContact(Rigidbody3DContact::Below) &&
        fallingCollider->GetOverlapping().empty();

    // 바닥에서 위로 향하는 속도는 분리 이동이다. 닿은 면에서 바깥으로 떠나는 것은 막지 않는다.
    fallingBody->SetVelocity({ 0.0f, 5.0f, 0.0f });
    floorPhysics.Simulate(floorFixture.sceneManager, Physics3DSystem::GetFixedDeltaTime());
    const bool canJumpAway = falling->GetTransform().GetWorldPosition().GetY() > 1.0f &&
        fallingBody->GetVelocity().GetY() > 0.0f && !fallingBody->IsGrounded();

    // 한 fixed step에서 벽 하나보다 훨씬 멀리 가도 sweep은 통과를 허용하지 않는다. X만 막히고
    // Y는 계속 움직여 벽을 따라 미끄러지는지도 함께 본다.
    PhysicsFixture wallFixture;
    GameObject* const wallObject = wallFixture.scene->CreateGameObject("Wall");
    BoxCollider3D* const wallCollider = wallObject->AddComponent<BoxCollider3D>();
    wallCollider->SetSize({ 1.0f, 8.0f, 8.0f });

    GameObject* const runner = wallFixture.scene->CreateGameObject("Runner");
    runner->GetTransform().SetPosition({ -5.0f, 0.0f, 0.0f });
    BoxCollider3D* const runnerCollider = runner->AddComponent<BoxCollider3D>();
    Rigidbody3D* const runnerBody = runner->AddComponent<Rigidbody3D>();
    runnerBody->SetGravityScale(0.0f);
    runnerBody->SetVelocity({ 600.0f, 2.0f, 0.0f });
    const unsigned int wallSceneId = wallFixture.AddScene();
    Physics3DSystem wallPhysics;
    wallPhysics.SetGravity({ 0.0f, 0.0f, 0.0f });
    wallPhysics.Simulate(wallFixture.sceneManager, Physics3DSystem::GetFixedDeltaTime());
    const bool stopsAtWallAndSlides = wallSceneId != 0 && wallCollider && runnerCollider && runnerBody &&
        IsNearly(runner->GetTransform().GetWorldPosition().GetX(), -1.0f) &&
        IsNearly(runnerBody->GetVelocity().GetX(), 0.0f) &&
        runner->GetTransform().GetWorldPosition().GetY() > 0.0f &&
        runnerBody->HasContact(Rigidbody3DContact::Right);

    // Z도 X/Y와 같은 sweep 규칙을 쓴다. +Z로 달려 전면 벽에 닿으면 전진 속도만 사라진다.
    PhysicsFixture depthFixture;
    GameObject* const depthWall = depthFixture.scene->CreateGameObject("DepthWall");
    BoxCollider3D* const depthWallCollider = depthWall->AddComponent<BoxCollider3D>();
    depthWallCollider->SetSize({ 8.0f, 8.0f, 1.0f });

    GameObject* const depthRunner = depthFixture.scene->CreateGameObject("DepthRunner");
    depthRunner->GetTransform().SetPosition({ 0.0f, 0.0f, -5.0f });
    BoxCollider3D* const depthRunnerCollider = depthRunner->AddComponent<BoxCollider3D>();
    Rigidbody3D* const depthRunnerBody = depthRunner->AddComponent<Rigidbody3D>();
    depthRunnerBody->SetGravityScale(0.0f);
    depthRunnerBody->SetVelocity({ 0.0f, 2.0f, 600.0f });
    const unsigned int depthSceneId = depthFixture.AddScene();
    Physics3DSystem depthPhysics;
    depthPhysics.SetGravity({ 0.0f, 0.0f, 0.0f });
    depthPhysics.Simulate(depthFixture.sceneManager, Physics3DSystem::GetFixedDeltaTime());
    const bool stopsAtDepthWallAndSlides = depthSceneId != 0 && depthWallCollider &&
        depthRunnerCollider && depthRunnerBody &&
        IsNearly(depthRunner->GetTransform().GetWorldPosition().GetZ(), -1.0f) &&
        IsNearly(depthRunnerBody->GetVelocity().GetZ(), 0.0f) &&
        depthRunner->GetTransform().GetWorldPosition().GetY() > 0.0f &&
        depthRunnerBody->HasContact(Rigidbody3DContact::Forward);

    // Trigger는 겹침 목록에는 남지만 고체 sweep에서는 건너뛴다.
    PhysicsFixture triggerFixture;
    GameObject* const sensor = triggerFixture.scene->CreateGameObject("Sensor");
    sensor->GetTransform().SetPosition({ 1.0f, 0.0f, 0.0f });
    BoxCollider3D* const sensorCollider = sensor->AddComponent<BoxCollider3D>();
    sensorCollider->SetTrigger(true);

    GameObject* const visitor = triggerFixture.scene->CreateGameObject("Visitor");
    BoxCollider3D* const visitorCollider = visitor->AddComponent<BoxCollider3D>();
    Rigidbody3D* const visitorBody = visitor->AddComponent<Rigidbody3D>();
    visitorBody->SetGravityScale(0.0f);
    visitorBody->SetVelocity({ 60.0f, 0.0f, 0.0f });
    const unsigned int triggerSceneId = triggerFixture.AddScene();
    Physics3DSystem triggerPhysics;
    triggerPhysics.SetGravity({ 0.0f, 0.0f, 0.0f });
    triggerPhysics.Simulate(triggerFixture.sceneManager, Physics3DSystem::GetFixedDeltaTime());
    const bool triggerDoesNotBlock = triggerSceneId != 0 && sensorCollider && visitorCollider &&
        visitorBody && IsNearly(visitor->GetTransform().GetWorldPosition().GetX(), 1.0f) &&
        IsNearly(visitorBody->GetVelocity().GetX(), 60.0f) &&
        visitorCollider->IsOverlapping(sensorCollider->GetInstanceId());

    // 활성 몸체끼리의 반발은 아직 없다. 순회 중 먼저 움직인 몸체를 정적으로 보지 않고 둘 다
    // 같은 속도로 서로를 지난다.
    PhysicsFixture bodiesFixture;
    GameObject* const leftBodyObject = bodiesFixture.scene->CreateGameObject("LeftBody");
    leftBodyObject->GetTransform().SetPosition({ -2.0f, 0.0f, 0.0f });
    BoxCollider3D* const leftBodyCollider = leftBodyObject->AddComponent<BoxCollider3D>();
    Rigidbody3D* const leftBody = leftBodyObject->AddComponent<Rigidbody3D>();
    leftBody->SetGravityScale(0.0f);
    leftBody->SetVelocity({ 120.0f, 0.0f, 0.0f });

    GameObject* const rightBodyObject = bodiesFixture.scene->CreateGameObject("RightBody");
    rightBodyObject->GetTransform().SetPosition({ 2.0f, 0.0f, 0.0f });
    BoxCollider3D* const rightBodyCollider = rightBodyObject->AddComponent<BoxCollider3D>();
    Rigidbody3D* const rightBody = rightBodyObject->AddComponent<Rigidbody3D>();
    rightBody->SetGravityScale(0.0f);
    rightBody->SetVelocity({ -120.0f, 0.0f, 0.0f });
    const unsigned int bodiesSceneId = bodiesFixture.AddScene();
    Physics3DSystem bodiesPhysics;
    bodiesPhysics.SetGravity({ 0.0f, 0.0f, 0.0f });
    bodiesPhysics.Simulate(bodiesFixture.sceneManager, Physics3DSystem::GetFixedDeltaTime());
    bodiesPhysics.Simulate(bodiesFixture.sceneManager, Physics3DSystem::GetFixedDeltaTime());
    const bool activeBodiesPassThrough = bodiesSceneId != 0 && leftBodyCollider && leftBody &&
        rightBodyCollider && rightBody &&
        IsNearly(leftBodyObject->GetTransform().GetWorldPosition().GetX(), 2.0f) &&
        IsNearly(rightBodyObject->GetTransform().GetWorldPosition().GetX(), -2.0f) &&
        IsNearly(leftBody->GetVelocity().GetX(), 120.0f) &&
        IsNearly(rightBody->GetVelocity().GetX(), -120.0f);

    // 다른 차원의 활성 몸체가 가진 3D 콜라이더도 정적 벽이 될 수는 없다. 그러면 2D와 3D의
    // 호출 순서가 충돌 결과를 정하게 되므로, 고체 충돌에서는 동적 몸체처럼 함께 건너뛴다.
    PhysicsFixture otherDimensionFixture;
    GameObject* const otherDimensionTarget = otherDimensionFixture.scene->CreateGameObject("OtherDimension");
    BoxCollider3D* const otherDimensionCollider = otherDimensionTarget->AddComponent<BoxCollider3D>();
    Rigidbody2D* const otherDimensionBody = otherDimensionTarget->AddComponent<Rigidbody2D>();
    otherDimensionBody->SetGravityScale(0.0f);

    GameObject* const otherDimensionRunner = otherDimensionFixture.scene->CreateGameObject("OtherDimensionRunner");
    otherDimensionRunner->GetTransform().SetPosition({ -5.0f, 0.0f, 0.0f });
    BoxCollider3D* const otherDimensionRunnerCollider =
        otherDimensionRunner->AddComponent<BoxCollider3D>();
    Rigidbody3D* const otherDimensionRunnerBody = otherDimensionRunner->AddComponent<Rigidbody3D>();
    otherDimensionRunnerBody->SetGravityScale(0.0f);
    otherDimensionRunnerBody->SetVelocity({ 600.0f, 0.0f, 0.0f });
    const unsigned int otherDimensionSceneId = otherDimensionFixture.AddScene();
    Physics3DSystem otherDimensionPhysics;
    otherDimensionPhysics.SetGravity({ 0.0f, 0.0f, 0.0f });
    otherDimensionPhysics.Simulate(
        otherDimensionFixture.sceneManager, Physics3DSystem::GetFixedDeltaTime());
    const bool otherDimensionBodyDoesNotBlock = otherDimensionSceneId != 0 &&
        otherDimensionCollider && otherDimensionBody && otherDimensionRunnerCollider &&
        otherDimensionRunnerBody &&
        IsNearly(otherDimensionRunner->GetTransform().GetWorldPosition().GetX(), 5.0f) &&
        IsNearly(otherDimensionRunnerBody->GetVelocity().GetX(), 600.0f);

    // 첫 3D 몸체가 꺼져 있어도, 뒤의 활성 몸체가 가진 콜라이더를 정적 벽으로 보아서는 안 된다.
    PhysicsFixture laterBodyFixture;
    GameObject* const laterBodyTarget = laterBodyFixture.scene->CreateGameObject("LaterBody");
    BoxCollider3D* const laterBodyCollider = laterBodyTarget->AddComponent<BoxCollider3D>();
    Rigidbody3D* const disabledBody = laterBodyTarget->AddComponent<Rigidbody3D>();
    disabledBody->SetEnabled(false);
    Rigidbody3D* const activeLaterBody = laterBodyTarget->AddComponent<Rigidbody3D>();
    activeLaterBody->SetGravityScale(0.0f);

    GameObject* const laterBodyRunner = laterBodyFixture.scene->CreateGameObject("LaterBodyRunner");
    laterBodyRunner->GetTransform().SetPosition({ -5.0f, 0.0f, 0.0f });
    BoxCollider3D* const laterBodyRunnerCollider = laterBodyRunner->AddComponent<BoxCollider3D>();
    Rigidbody3D* const laterBodyRunnerBody = laterBodyRunner->AddComponent<Rigidbody3D>();
    laterBodyRunnerBody->SetGravityScale(0.0f);
    laterBodyRunnerBody->SetVelocity({ 600.0f, 0.0f, 0.0f });
    const unsigned int laterBodySceneId = laterBodyFixture.AddScene();
    Physics3DSystem laterBodyPhysics;
    laterBodyPhysics.SetGravity({ 0.0f, 0.0f, 0.0f });
    laterBodyPhysics.Simulate(laterBodyFixture.sceneManager, Physics3DSystem::GetFixedDeltaTime());
    const bool laterActiveBodyDoesNotBlock = laterBodySceneId != 0 && laterBodyCollider &&
        disabledBody && activeLaterBody && laterBodyRunnerCollider && laterBodyRunnerBody &&
        IsNearly(laterBodyRunner->GetTransform().GetWorldPosition().GetX(), 5.0f) &&
        IsNearly(laterBodyRunnerBody->GetVelocity().GetX(), 600.0f);

    // Enter/Exit는 fixed step 횟수가 아니라 프레임 마지막 위치에서 한 번만 갱신한다.
    PhysicsFixture overlapFixture;
    GameObject* const firstOverlap = overlapFixture.scene->CreateGameObject("FirstOverlap");
    BoxCollider3D* const firstOverlapCollider = firstOverlap->AddComponent<BoxCollider3D>();
    GameObject* const secondOverlap = overlapFixture.scene->CreateGameObject("SecondOverlap");
    BoxCollider3D* const secondOverlapCollider = secondOverlap->AddComponent<BoxCollider3D>();
    const unsigned int overlapSceneId = overlapFixture.AddScene();
    Physics3DSystem overlapPhysics;
    const std::size_t initialPairs = overlapPhysics.Synchronize(overlapFixture.sceneManager);
    const bool enteredInitially = firstOverlapCollider && secondOverlapCollider &&
        firstOverlapCollider->GetEntered().size() == 1 && secondOverlapCollider->GetEntered().size() == 1 &&
        firstOverlapCollider->GetEntered()[0] == secondOverlapCollider->GetInstanceId() &&
        secondOverlapCollider->GetEntered()[0] == firstOverlapCollider->GetInstanceId();
    overlapPhysics.Synchronize(overlapFixture.sceneManager);
    const bool enteredOnlyOnce = firstOverlapCollider && secondOverlapCollider &&
        firstOverlapCollider->GetEntered().empty() && secondOverlapCollider->GetEntered().empty();
    secondOverlap->GetTransform().SetPosition({ 2.0f, 0.0f, 0.0f });
    overlapPhysics.Synchronize(overlapFixture.sceneManager);
    const bool overlapEventsAreFrameBased = overlapSceneId != 0 && initialPairs == 1 && enteredInitially &&
        enteredOnlyOnce &&
        firstOverlapCollider->GetExited().size() == 1 && secondOverlapCollider->GetExited().size() == 1 &&
        firstOverlapCollider->GetExited()[0] == secondOverlapCollider->GetInstanceId() &&
        secondOverlapCollider->GetExited()[0] == firstOverlapCollider->GetInstanceId();

    // 2D와 3D 몸체가 같은 Transform을 적분하는 설정은 어느 시스템도 선택하지 않는다.
    PhysicsFixture mixedFixture;
    GameObject* const mixedObject = mixedFixture.scene->CreateGameObject("MixedBody");
    Rigidbody2D* const mixed2D = mixedObject->AddComponent<Rigidbody2D>();
    Rigidbody3D* const mixed3D = mixedObject->AddComponent<Rigidbody3D>();
    mixed2D->SetVelocity({ 6.0f, 4.0f });
    mixed3D->SetVelocity({ 3.0f, 2.0f, 1.0f });
    const unsigned int mixedSceneId = mixedFixture.AddScene();
    Physics2DSystem mixed2DPhysics;
    Physics3DSystem mixed3DPhysics;
    mixed2DPhysics.SetGravity({ 0.0f, 0.0f });
    mixed3DPhysics.SetGravity({ 0.0f, 0.0f, 0.0f });
    mixed2DPhysics.Simulate(mixedFixture.sceneManager, Physics2DSystem::GetFixedDeltaTime());
    mixed3DPhysics.Simulate(mixedFixture.sceneManager, Physics3DSystem::GetFixedDeltaTime());
    const GameEngine::Math::Vector3 mixedPosition = mixedObject->GetTransform().GetWorldPosition();
    const bool mixedDimensionsDoNotMove = mixedSceneId != 0 && mixed2D && mixed3D &&
        IsNearly(mixedPosition.GetX(), 0.0f) && IsNearly(mixedPosition.GetY(), 0.0f) &&
        IsNearly(mixedPosition.GetZ(), 0.0f);

    // 혼합 몸체가 있는 부모 자신만 멈춰야 한다. 자식의 독립 3D 몸체까지 건너뛰면 계층 구조가
    // 물리 참여를 뜻하지 않게 된다.
    PhysicsFixture mixedParentFixture;
    GameObject* const mixedParent = mixedParentFixture.scene->CreateGameObject("MixedParent");
    Rigidbody2D* const mixedParent2D = mixedParent->AddComponent<Rigidbody2D>();
    Rigidbody3D* const mixedParent3D = mixedParent->AddComponent<Rigidbody3D>();
    GameObject* const independentChild = mixedParentFixture.scene->CreateGameObject("IndependentChild");
    const bool childWasParented =
        independentChild->GetTransform().SetParent(&mixedParent->GetTransform());
    Rigidbody3D* const independentChildBody = independentChild->AddComponent<Rigidbody3D>();
    independentChildBody->SetGravityScale(0.0f);
    independentChildBody->SetVelocity({ 6.0f, 0.0f, 0.0f });
    const unsigned int mixedParentSceneId = mixedParentFixture.AddScene();
    Physics3DSystem mixedParentPhysics;
    mixedParentPhysics.SetGravity({ 0.0f, 0.0f, 0.0f });
    mixedParentPhysics.Simulate(
        mixedParentFixture.sceneManager, Physics3DSystem::GetFixedDeltaTime());
    const bool mixedParentLeavesChildIndependent = mixedParentSceneId != 0 && mixedParent2D &&
        mixedParent3D && childWasParented && independentChildBody &&
        IsNearly(mixedParent->GetTransform().GetWorldPosition().GetX(), 0.0f) &&
        IsNearly(independentChild->GetTransform().GetWorldPosition().GetX(), 0.1f);

    // 혼합 몸체가 된 순간에는 마지막 fixed step의 접촉도 더 이상 유효하지 않다. 다음 3D fixed
    // step에서 이를 비워, 이미 바닥을 떠난 설정이 grounded로 남지 않게 한다.
    PhysicsFixture mixedContactFixture;
    GameObject* const contactFloor = mixedContactFixture.scene->CreateGameObject("ContactFloor");
    BoxCollider3D* const contactFloorCollider = contactFloor->AddComponent<BoxCollider3D>();
    contactFloorCollider->SetSize({ 12.0f, 1.0f, 12.0f });
    GameObject* const contactBodyObject = mixedContactFixture.scene->CreateGameObject("ContactBody");
    contactBodyObject->GetTransform().SetPosition({ 0.0f, 1.0f, 0.0f });
    BoxCollider3D* const contactBodyCollider = contactBodyObject->AddComponent<BoxCollider3D>();
    Rigidbody3D* const contactBody = contactBodyObject->AddComponent<Rigidbody3D>();
    contactBody->SetGravityScale(0.0f);
    contactBody->SetVelocity({ 0.0f, -1.0f, 0.0f });
    const unsigned int mixedContactSceneId = mixedContactFixture.AddScene();
    Physics3DSystem mixedContactPhysics;
    mixedContactPhysics.SetGravity({ 0.0f, 0.0f, 0.0f });
    mixedContactPhysics.Simulate(mixedContactFixture.sceneManager, Physics3DSystem::GetFixedDeltaTime());
    const bool contactWasGrounded = contactBody->IsGrounded();
    Rigidbody2D* const contactConflict = contactBodyObject->AddComponent<Rigidbody2D>();
    mixedContactPhysics.Simulate(mixedContactFixture.sceneManager, Physics3DSystem::GetFixedDeltaTime());
    const bool mixedDimensionsClearContacts = mixedContactSceneId != 0 && contactFloorCollider &&
        contactBodyCollider && contactBody && contactConflict && contactWasGrounded &&
        !contactBody->IsGrounded() && contactBody->GetContacts() == Rigidbody3DContact::None;

    return Expect(aabbKeepsPhysicsVolumeRules, "a 3D AABB should keep the 2D volume and boundary rules") &&
        Expect(gameRunsPhysics, "a game frame should run 3D rigidbody physics after scene updates") &&
        Expect(fallsAndRests, "a 3D rigidbody should fall onto a solid floor and report grounded") &&
        Expect(canJumpAway, "a 3D rigidbody should be able to jump away from a touching floor") &&
        Expect(stopsAtWallAndSlides, "a 3D rigidbody should stop on an X wall without losing free-axis motion") &&
        Expect(stopsAtDepthWallAndSlides, "a 3D rigidbody should stop on a Z wall without losing free-axis motion") &&
        Expect(triggerDoesNotBlock, "a 3D trigger should report overlap without blocking a rigidbody") &&
        Expect(activeBodiesPassThrough, "active 3D rigidbodies should pass through one another") &&
        Expect(otherDimensionBodyDoesNotBlock, "a 2D body should not make a 3D collider a static wall") &&
        Expect(laterActiveBodyDoesNotBlock, "an active later 3D body should not be treated as static") &&
        Expect(overlapEventsAreFrameBased, "3D collider enter and exit should use final frame positions") &&
        Expect(mixedDimensionsDoNotMove, "mixed 2D and 3D rigidbodies should not compete for one transform") &&
        Expect(mixedParentLeavesChildIndependent, "a mixed parent should not skip an independent child body") &&
        Expect(mixedDimensionsClearContacts, "mixed 2D and 3D rigidbodies should clear stale contacts");
}

static const TestSupport::Registration gRigidbody3DTests{
    "Physics", "rigidbody 3D tests should pass", RunRigidbody3DTests };
