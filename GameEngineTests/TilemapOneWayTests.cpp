#include "TilemapOneWayTests.h"

#include <cmath>
#include <memory>
#include <span>
#include <string>

#include "Core/Json.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Runtime/BoxCollider2D.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Physics2DSystem.h"
#include "Runtime/Rigidbody2D.h"
#include "Runtime/Scene.h"
#include "Runtime/TilemapCollider2D.h"
#include "Runtime/TilemapRenderer.h"
#include "Runtime/Transform.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Serialization/SceneSerializer.h"
#include "TestSupport.h"

namespace
{
    namespace Runtime = GameEngine::Runtime;
    namespace Math = GameEngine::Math;
    using TestSupport::Expect;

    struct Fixture
    {
        Runtime::Game game{ nullptr, nullptr };
        Runtime::Scene* scene = nullptr;
        Runtime::GameObject* mapObject = nullptr;
        Runtime::TilemapRenderer* map = nullptr;
        Runtime::TilemapCollider2D* platform = nullptr;
        Runtime::GameObject* player = nullptr;
        Runtime::BoxCollider2D* box = nullptr;
        Runtime::Rigidbody2D* body = nullptr;

        Fixture()
        {
            auto ownedScene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "OneWay");
            scene = ownedScene.get();
            mapObject = scene->CreateGameObject("Map");
            map = mapObject->AddComponent<Runtime::TilemapRenderer>();
            map->SetColumns(3);
            map->SetRows(2);
            map->SetTiles({ 0, 0, 0, 0, 0, 0 });
            platform = mapObject->AddComponent<Runtime::TilemapCollider2D>();
            platform->SetOneWay(true);
            player = scene->CreateGameObject("Player");
            box = player->AddComponent<Runtime::BoxCollider2D>();
            body = player->AddComponent<Runtime::Rigidbody2D>();
            body->SetGravityScale(0);
            game.GetPhysics2DSystem().SetGravity({ 0, 0 });
            static_cast<void>(game.AddScene(std::move(ownedScene)));
        }

        void Place(const Math::Vector2 position, const Math::Vector2 velocity)
        {
            player->GetTransform().SetPosition({ position.GetX(), position.GetY(), 0 });
            body->ResetMotion();
            body->SetVelocity(velocity);
        }

        void Step() { game.Update(Runtime::Physics2DSystem::GetFixedDeltaTime()); }
        [[nodiscard]] float X() const { return player->GetTransform().GetWorldPosition().GetX(); }
        [[nodiscard]] float Y() const { return player->GetTransform().GetWorldPosition().GetY(); }
    };

    [[nodiscard]] bool Nearly(const float a, const float b)
    {
        return std::abs(a - b) < 0.0001f;
    }

    bool PassAndLand()
    {
        Fixture f;
        f.Place({ 1.5f, -1 }, { 0, 240 });
        f.Step();
        if (!Expect(Nearly(f.Y(), 3) && f.body->GetContacts() == Runtime::Rigidbody2DContact::None,
            "one-way tiles should allow a complete upward crossing")) return false;
        f.body->SetVelocity({ 0, -600 });
        f.Step();
        if (!Expect(Nearly(f.Y(), 2.5f) && f.body->IsGrounded() && f.body->GetVelocity().GetY() == 0,
            "a fast fall should stop at the exposed top of stacked tiles")) return false;

        f.body->SetGravityScale(1);
        f.game.GetPhysics2DSystem().SetGravity({ 0, -10 });
        f.body->SetVelocity({ 3, 0 });
        for (int step = 0; step < 15; ++step) f.Step();
        if (!Expect(Nearly(f.X(), 2.25f) && Nearly(f.Y(), 2.5f) && f.body->IsGrounded(),
            "resting contact should persist while crossing a seam without horizontal snagging")) return false;
        f.body->SetVelocity({ 0, 12 });
        f.Step();
        if (!Expect(f.Y() > 2.5f && !f.body->IsGrounded(), "jumping should leave a resting one-way surface")) return false;

        f.body->SetGravityScale(0);
        f.Place({ -1, 1 }, { 300, 0 });
        f.Step();
        if (!Expect(Nearly(f.X(), 4) && f.body->GetContacts() == Runtime::Rigidbody2DContact::None,
            "one-way tile sides should not block horizontal travel")) return false;
        f.Place({ 1.5f, 1.75f }, { 0, -120 });
        f.Step();
        if (!Expect(Nearly(f.Y(), -0.25f) && !f.body->IsGrounded(),
            "an initially embedded body must not land on an internal stacked-tile face")) return false;
        f.Place({ 3.5f, 3 }, { 0, -300 });
        f.Step();
        return Expect(Nearly(f.Y(), -2) && !f.body->IsGrounded(),
            "an exact horizontal edge touch without area must not catch the platform");
    }

    bool SparseAndChangedTiles()
    {
        Fixture f;
        f.map->SetTiles({ 0, -1, 0, -1, -1, 0 });
        f.Place({ 0.5f, 3 }, { 0, -600 });
        f.Step();
        if (!Expect(Nearly(f.Y(), 1.5f) && f.body->IsGrounded(),
            "an empty cell above should expose the lower tile top")) return false;
        f.Place({ 1.5f, 3 }, { 0, -300 });
        f.Step();
        if (!Expect(Nearly(f.Y(), -2) && !f.body->IsGrounded(), "a tilemap hole must remain passable")) return false;
        static_cast<void>(f.map->SetTile(0, 1, 0));
        f.Place({ 0.5f, 3 }, { 0, -300 });
        f.Step();
        if (!Expect(Nearly(f.Y(), 2.5f), "painting a tile must immediately move its exposed top")) return false;
        f.platform->SetOneWay(false);
        f.Place({ 0.5f, -1 }, { 0, 300 });
        f.Step();
        if (!Expect(Nearly(f.Y(), -0.5f) && f.body->HasContact(Runtime::Rigidbody2DContact::Above),
            "disabling one-way must immediately restore solid underside collision")) return false;
        f.platform->SetOneWay(true);
        f.body->SetVelocity({ 0, 300 });
        f.Step();
        return Expect(Nearly(f.Y(), 4.5f) && !f.body->HasContact(Runtime::Rigidbody2DContact::Above),
            "enabling one-way must let the body leave an existing underside contact");
    }

    bool ElevatedRestingContact()
    {
        // top=32, center=33.297, height=2.56의 첫 착지는 foot=31.999998 정도가 된다.
        // 고정 1e-6 허용치는 다음 스텝에 이를 "이미 아래"로 오인해 발판을 통과시킨다.
        const float topHeights[]{ 32, -32, 8192, -8192, 100000, -100000 };
        for (const float top : topHeights)
        {
            Fixture f;
            f.mapObject->GetTransform().SetPosition({ 0, top - 2, 0 });
            f.box->SetSize({ 0.9f, 2.56f });
            f.Place({ 1.5f, top + 1.297f }, { 0, -1.2f });
            f.Step();
            const float landedY = f.Y();
            if (!Expect(f.body->IsGrounded() && f.body->GetVelocity().GetY() == 0 &&
                std::abs(f.box->GetWorldBounds().min.GetY() - top) <= 0.015625f,
                "noninteger collider height should land within float precision at signed elevated coordinates")) return false;
            f.body->SetGravityScale(1);
            f.game.GetPhysics2DSystem().SetGravity({ 0, -10 });
            for (int step = 0; step < 120; ++step)
            {
                f.Step();
                if (!Expect(f.body->IsGrounded() && f.body->GetVelocity().GetY() == 0 && f.Y() == landedY,
                    "rounding below an elevated platform must not lose resting contact on later gravity steps")) return false;
            }

            // 0.125는 가장 큰 시험 좌표에서도 여러 ULP다. 실제로 아래에 있는 몸체는 잡지 않는다.
            f.Place({ 1.5f, top + 1.28f - 0.125f }, { 0, -60 });
            const float embeddedY = f.Y();
            f.Step();
            if (!Expect(!f.body->IsGrounded() && f.Y() < embeddedY - 0.9f,
                "coordinate-aware contact tolerance must not catch a body genuinely below the top")) return false;
        }
        return true;
    }

    bool TransformsAndTriggers()
    {
        Fixture f;
        f.mapObject->GetTransform().SetPosition({ 10, 8, 0 });
        f.mapObject->GetTransform().SetScale({ -2, -3, 1 });
        f.map->SetCellSize({ 0.5f, 0.5f });
        f.Place({ 8.5f, 12 }, { 0, -600 });
        f.Step();
        if (!Expect(Nearly(f.Y(), 8.5f) && f.body->IsGrounded(),
            "negative XY scale should land on the actual world +Y surface")) return false;
        f.Place({ 8.5f, 7.5f }, { 0, -300 });
        f.Step();
        if (!Expect(Nearly(f.Y(), 2.5f) && !f.body->IsGrounded(),
            "negative Y scale should skip faces covered by the preceding local row")) return false;

        f.mapObject->GetTransform().SetScale({ 2, 3, 1 });
        f.Place({ 11.5f, 15 }, { 0, -600 });
        f.Step();
        if (!Expect(Nearly(f.Y(), 11.5f), "nonuniform positive scale and translation should set the correct top")) return false;

        f.platform->SetTrigger(true);
        f.Place({ 11.5f, 12 }, { 0, -120 });
        f.Step();
        if (!Expect(Nearly(f.Y(), 10) && !f.body->IsGrounded() &&
            f.box->IsOverlapping(f.platform->GetInstanceId()) && f.box->GetEntered().size() == 1,
            "a one-way trigger should report volume entry while allowing a downward crossing")) return false;
        f.body->SetVelocity({ 0, -300 });
        f.Step();
        if (!Expect(!f.box->IsOverlapping(f.platform->GetInstanceId()) && f.box->GetExited().size() == 1,
            "leaving a one-way trigger volume should report exit")) return false;
        f.platform->SetTrigger(false);
        f.Place({ 11.5f, 10 }, { 0, 0 });
        f.Step();
        return Expect(f.platform->OverlapsBox(f.box->GetWorldBounds()) &&
            f.box->IsOverlapping(f.platform->GetInstanceId()) && !f.body->IsGrounded(),
            "one-way solid overlap queries should retain volume semantics even without landing");
    }

    bool Serialization()
    {
        namespace Serialization = GameEngine::Serialization;
        static_cast<void>(Serialization::RegisterRuntimeComponentFactories());
        Fixture f;
        Runtime::TilemapCollider2D defaults;
        if (!Expect(!defaults.IsOneWay(), "tilemaps without an explicit setting should remain solid")) return false;
        f.platform->SetTrigger(true);
        const auto json = Serialization::SceneSerializer::SaveComponentToJson(*f.platform);
        if (!Expect(json && json->Value("oneWay", false),
            "one-way should be an inspector property included in component snapshots")) return false;
        const std::string text = Serialization::SceneSerializer::SaveToText(*f.scene);
        const auto restored = Serialization::SceneSerializer::LoadFromBytes(
            std::as_bytes(std::span(text.data(), text.size())), "OneWay.scene", f.game.GetRuntimeContext());
        const auto* mapObject = restored ? restored->FindGameObject("Map") : nullptr;
        const auto* collider = mapObject ? mapObject->GetComponent<Runtime::TilemapCollider2D>() : nullptr;
        auto* legacyObject = f.scene->CreateGameObject("Legacy");
        static_cast<void>(Serialization::SceneSerializer::LoadComponentIntoGameObject(
            GameEngine::Core::Json::Parse(R"({"type":"TilemapCollider2D"})"), *legacyObject));
        const auto* legacy = legacyObject->GetComponent<Runtime::TilemapCollider2D>();
        return Expect(collider && collider->IsOneWay() && collider->IsTrigger(),
                "one-way and trigger settings should survive a complete scene roundtrip") &&
            Expect(legacy && !legacy->IsOneWay(), "legacy serialized colliders should keep solid behavior");
    }
}

bool RunTilemapOneWayTests()
{
    return PassAndLand() && SparseAndChangedTiles() && ElevatedRestingContact() &&
        TransformsAndTriggers() && Serialization();
}

static const TestSupport::Registration gTilemapOneWayTests{
    "Physics", "one-way tilemap top surfaces should pass", RunTilemapOneWayTests };
