#include "Rigidbody2DResetTests.h"

#include <memory>

#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Runtime/BoxCollider2D.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Rigidbody2D.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"
#include "TestSupport.h"

bool RunRigidbody2DResetTests()
{
    namespace Runtime = GameEngine::Runtime;
    using TestSupport::Expect;
    Runtime::Game game(nullptr, nullptr);
    auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "MotionReset");
    auto* floor = scene->CreateGameObject("Floor");
    floor->AddComponent<Runtime::BoxCollider2D>()->SetSize({ 20, 1 });
    auto* player = scene->CreateGameObject("Player");
    player->GetTransform().SetPosition({ 0, 1, 0 });
    static_cast<void>(player->AddComponent<Runtime::BoxCollider2D>());
    auto* body = player->AddComponent<Runtime::Rigidbody2D>();
    body->SetGravityScale(0.5f);
    if (!Expect(game.GetSceneManager().AddScene(std::move(scene)) != 0, "reset fixture should adopt its scene")) return false;
    game.Update(1.0f / 60.0f);
    if (!Expect(body->IsGrounded(), "reset fixture should begin with an actual floor contact")) return false;

    const auto position = player->GetTransform().GetWorldPosition();
    body->SetVelocity({ 8, -30 });
    body->ResetMotion();
    if (!Expect(body->GetVelocity().GetX() == 0 && body->GetVelocity().GetY() == 0 &&
            body->GetContacts() == Runtime::Rigidbody2DContact::None && !body->IsGrounded(),
            "ResetMotion should immediately clear all velocity and contact state") ||
        !Expect(player->GetTransform().GetWorldPosition() == position && body->GetGravityScale() == 0.5f,
            "ResetMotion should preserve transform and gravity settings")) return false;

    game.Update(0);
    if (!Expect(!body->IsGrounded(), "a frame without physics should not restore old ground contact")) return false;
    game.Update(1.0f / 60.0f);
    if (!Expect(body->IsGrounded(), "new physics should still be able to reacquire floor contact")) return false;
    body->SetEnabled(false);
    body->ResetMotion();
    body->ResetMotion();
    return Expect(!body->IsGrounded() && body->GetContacts() == Runtime::Rigidbody2DContact::None &&
        body->GetVelocity().GetX() == 0 && body->GetVelocity().GetY() == 0,
        "motion reset should remain safe and idempotent while physics is disabled");
}

static const TestSupport::Registration gRigidbody2DResetTests{
    "Physics", "rigidbody 2D motion reset should pass", RunRigidbody2DResetTests };
