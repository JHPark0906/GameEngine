#include "SparseTilemapPairTests.h"

#include <memory>
#include <utility>

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

bool RunSparseTilemapPairTests()
{
    namespace Runtime = GameEngine::Runtime;
    Runtime::ObjectRegistry registry;
    Runtime::Input input;
    Runtime::RuntimeContext context(registry, input);
    Runtime::SceneManager manager(context);
    auto scene = std::make_unique<Runtime::Scene>(context, "SparsePairs");
    auto* first = scene->CreateGameObject("First");
    auto* second = scene->CreateGameObject("Second");
    auto* firstMap = first->AddComponent<Runtime::TilemapRenderer>();
    auto* secondMap = second->AddComponent<Runtime::TilemapRenderer>();
    for (auto* map : { firstMap, secondMap })
    {
        map->SetColumns(3);
        map->SetRows(1);
        map->SetCellSize({ 1.0f, 1.0f });
    }
    static_cast<void>(firstMap->SetTile(0, 0, 0));
    static_cast<void>(secondMap->SetTile(2, 0, 0));
    auto* firstCollider = first->AddComponent<Runtime::TilemapCollider2D>();
    auto* secondCollider = second->AddComponent<Runtime::TilemapCollider2D>();
    if (!TestSupport::Expect(manager.AddScene(std::move(scene)) != 0, "sparse fixture must load")) return false;
    Runtime::Physics2DSystem physics;
    bool passed = TestSupport::Expect(physics.Synchronize(manager) == 0 &&
        firstCollider->GetOverlapping().empty() && secondCollider->GetOverlapping().empty(),
        "disjoint painted cells must not collide merely because their grid bounds overlap");
    second->GetTransform().SetPosition({ -2.0f, 0.0f, 0.0f });
    passed = TestSupport::Expect(physics.Synchronize(manager) == 1 &&
        firstCollider->IsOverlapping(secondCollider->GetInstanceId()) &&
        secondCollider->IsOverlapping(firstCollider->GetInstanceId()) &&
        firstCollider->GetEntered().size() == 1,
        "moving painted cells onto each other must report one symmetric enter") && passed;
    second->GetTransform().SetPosition({ -1.0f, 0.0f, 0.0f });
    passed = TestSupport::Expect(physics.Synchronize(manager) == 0 &&
        firstCollider->GetExited().size() == 1,
        "touching cell edges must separate and report an exit") && passed;
    second->GetTransform().SetScale({ -1.0f, 1.0f, 1.0f });
    second->GetTransform().SetPosition({ 3.0f, 0.0f, 0.0f });
    return TestSupport::Expect(physics.Synchronize(manager) == 1,
        "negative scale must preserve occupied-cell overlap") && passed;
}

static const TestSupport::Registration gSparseTilemapPairTests{
    "Physics", "sparse tilemap pairs must compare occupied cells", RunSparseTilemapPairTests };
