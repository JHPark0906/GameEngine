#include "ScenePendingRemovalTests.h"

#include <array>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "Runtime/ComponentType.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/MonoBehaviour.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/Transform.h"
#include "TestSupport.h"

namespace
{
namespace Runtime = GameEngine::Runtime;
using TestSupport::Expect;

class MutationProbe final : public Runtime::MonoBehaviour
{
public:
    static const Runtime::ComponentType& StaticType()
    {
        static const Runtime::ComponentType type{ "ScenePendingRemovalProbe", &Runtime::MonoBehaviour::StaticType() };
        return type;
    }
    const Runtime::ComponentType& GetComponentType() const override { return StaticType(); }
    std::function<void()> update;
    std::function<void()> destroy;
    int* updates = nullptr;

protected:
    void Update(float) override
    {
        if (updates) ++*updates;
        auto action = std::exchange(update, {});
        if (action) action();
    }
    void OnDestroy() override
    {
        if (destroy) destroy();
    }
};

struct Fixture
{
    Runtime::ObjectRegistry registry;
    Runtime::Input input;
    Runtime::RuntimeContext context{ registry, input };
    Runtime::Scene scene{ context, "PendingRemoval" };
};

bool ExistingParentIncludesPendingAndLateChildren()
{
    std::array<int, 5> destroyed{};
    int pendingUpdates = 0;
    int survivorUpdates = 0;
    std::vector<unsigned int> ids;
    bool parenting = true;
    bool accepted = false;
    bool aliveUntilFlush = false;
    Fixture fixture;
    auto& scene = fixture.scene;
    auto* parent = scene.CreateGameObject("Parent");
    auto* existing = scene.CreateGameObject("ExistingChild");
    parenting = existing->GetTransform().SetParent(&parent->GetTransform());
    ids = { parent->GetInstanceId(), existing->GetInstanceId() };
    parent->AddComponent<MutationProbe>()->destroy = [&] { ++destroyed[0]; };
    existing->AddComponent<MutationProbe>()->destroy = [&] { ++destroyed[1]; };
    auto* driver = scene.CreateGameObject("Driver")->AddComponent<MutationProbe>();
    driver->update = [&]
    {
        auto* child = scene.CreateGameObject("PendingChild");
        auto* grandchild = scene.CreateGameObject("PendingGrandchild");
        parenting = child->GetTransform().SetParent(&existing->GetTransform()) && parenting;
        parenting = grandchild->GetTransform().SetParent(&child->GetTransform()) && parenting;
        ids.push_back(child->GetInstanceId());
        ids.push_back(grandchild->GetInstanceId());
        auto* childProbe = child->AddComponent<MutationProbe>();
        childProbe->updates = &pendingUpdates;
        childProbe->destroy = [&] { ++destroyed[2]; };
        grandchild->AddComponent<MutationProbe>()->destroy = [&] { ++destroyed[3]; };
        accepted = scene.RemoveGameObject(child->GetInstanceId()) &&
            scene.RemoveGameObject(parent->GetInstanceId()) && scene.RemoveGameObject(parent->GetInstanceId());
        // A child added after the deletion request must still be removed with this parent.
        auto* late = scene.CreateGameObject("LateChild");
        parenting = late->GetTransform().SetParent(&parent->GetTransform()) && parenting;
        ids.push_back(late->GetInstanceId());
        late->AddComponent<MutationProbe>()->destroy = [&] { ++destroyed[4]; };
        scene.CreateGameObject("Survivor")->AddComponent<MutationProbe>()->updates = &survivorUpdates;
        aliveUntilFlush = scene.GetGameObject(ids[2]) == child && destroyed[0] == 0 && destroyed[2] == 0;
    };
    scene.Update(0);
    bool absent = true;
    for (const auto id : ids) absent = !scene.GetGameObject(id) && !scene.FindObject(id) && absent;
    const bool once = destroyed == std::array<int, 5>{ 1, 1, 1, 1, 1 };
    const bool survivors = scene.GetGameObjects().size() == 2 && scene.FindGameObject("Survivor") &&
        scene.GetRootGameObjects().size() == 2;
    const bool absentAfterFlush = !scene.RemoveGameObject(ids[0]);
    scene.Update(0);
    return Expect(parenting && accepted && aliveUntilFlush, "pending removals should defer safely and accept repeated scheduling") &&
        Expect(absent && once && survivors && absentAfterFlush,
            "an existing parent must remove pending/late descendants exactly once without leaving orphan roots") &&
        Expect(pendingUpdates == 0 && survivorUpdates == 1,
            "removed pending children must never update; unrelated additions should update next frame");
}

bool PendingParentRemovesMixedDescendants()
{
    std::array<int, 4> destroyed{};
    std::vector<unsigned int> ids;
    bool parenting = true;
    bool accepted = false;
    int removedUpdates = 0;
    Fixture fixture;
    auto& scene = fixture.scene;
    auto* driver = scene.CreateGameObject("Driver")->AddComponent<MutationProbe>();
    auto* existing = scene.CreateGameObject("ExistingChild");
    auto* existingProbe = existing->AddComponent<MutationProbe>();
    existingProbe->destroy = [&] { ++destroyed[3]; };
    existingProbe->updates = &removedUpdates;
    driver->update = [&]
    {
        auto* parent = scene.CreateGameObject("PendingParent");
        auto* child = scene.CreateGameObject("PendingChild");
        auto* grandchild = scene.CreateGameObject("PendingGrandchild");
        parenting = child->GetTransform().SetParent(&parent->GetTransform()) &&
            grandchild->GetTransform().SetParent(&child->GetTransform()) &&
            existing->GetTransform().SetParent(&grandchild->GetTransform());
        ids = { parent->GetInstanceId(), child->GetInstanceId(), grandchild->GetInstanceId(), existing->GetInstanceId() };
        parent->AddComponent<MutationProbe>()->destroy = [&] { ++destroyed[0]; };
        child->AddComponent<MutationProbe>()->destroy = [&] { ++destroyed[1]; };
        grandchild->AddComponent<MutationProbe>()->destroy = [&] { ++destroyed[2]; };
        accepted = scene.RemoveGameObject(parent->GetInstanceId()) && scene.RemoveGameObject(parent->GetInstanceId());
    };
    scene.Update(0);
    bool absent = true;
    for (const auto id : ids) absent = !scene.GetGameObject(id) && absent;
    return Expect(parenting && accepted && absent && destroyed == std::array<int, 4>{ 1, 1, 1, 1 },
        "removing a pending parent should remove both pending and established descendants exactly once") &&
        Expect(removedUpdates == 0 && scene.GetGameObjects().size() == 1 && scene.GetRootGameObjects().size() == 1,
            "a removed mixed hierarchy must neither update nor leave any orphan root");
}

bool DestructionCanRemoveAndCreateObjects(const bool duringUpdate)
{
    std::array<int, 4> destroyed{};
    std::vector<unsigned int> doomedIds;
    bool selfAbsent = false;
    bool siblingRemoved = false;
    bool pendingRemoved = false;
    bool enumerationWorks = false;
    bool accepted = false;
    int survivorUpdates = 0;
    Fixture fixture;
    auto& scene = fixture.scene;
    auto* target = scene.CreateGameObject("Target");
    auto* sibling = scene.CreateGameObject("Sibling");
    const auto targetId = target->GetInstanceId();
    const auto siblingId = sibling->GetInstanceId();
    doomedIds = { targetId, siblingId };
    target->AddComponent<MutationProbe>()->destroy = [&]
    {
        ++destroyed[0];
        selfAbsent = !scene.GetGameObject(targetId) && !scene.RemoveGameObject(targetId);
        siblingRemoved = scene.RemoveGameObject(siblingId) && scene.RemoveGameObject(siblingId);
        auto* addedParent = scene.CreateGameObject("CallbackParent");
        auto* addedChild = scene.CreateGameObject("CallbackChild");
        const bool parented = addedChild->GetTransform().SetParent(&addedParent->GetTransform());
        doomedIds.push_back(addedParent->GetInstanceId());
        doomedIds.push_back(addedChild->GetInstanceId());
        addedParent->AddComponent<MutationProbe>()->destroy = [&] { ++destroyed[2]; };
        addedChild->AddComponent<MutationProbe>()->destroy = [&] { ++destroyed[3]; };
        pendingRemoved = parented && scene.RemoveGameObject(addedParent->GetInstanceId()) &&
            scene.RemoveGameObject(addedParent->GetInstanceId());
        scene.CreateGameObject("CallbackSurvivor")->AddComponent<MutationProbe>()->updates = &survivorUpdates;
        enumerationWorks = scene.FindGameObject("CallbackSurvivor") && !scene.GetObjects().empty();
    };
    sibling->AddComponent<MutationProbe>()->destroy = [&]
    {
        ++destroyed[1];
        selfAbsent = !scene.RemoveGameObject(targetId) && !scene.RemoveGameObject(siblingId) && selfAbsent;
        scene.CreateGameObject("OtherSurvivor")->AddComponent<MutationProbe>()->updates = &survivorUpdates;
    };
    if (duringUpdate)
    {
        scene.CreateGameObject("Driver")->AddComponent<MutationProbe>()->update = [&]
            { accepted = scene.RemoveGameObject(targetId); };
        scene.Update(0);
    }
    else
    {
        accepted = scene.RemoveGameObject(targetId);
    }
    bool absent = true;
    for (const auto id : doomedIds) absent = !scene.GetGameObject(id) && absent;
    const bool once = destroyed == std::array<int, 4>{ 1, 1, 1, 1 };
    const bool notUpdatedDuringFlush = survivorUpdates == 0;
    scene.Update(0);
    return Expect(accepted && selfAbsent && siblingRemoved && pendingRemoved && enumerationWorks,
        "OnDestroy should safely remove self/siblings and create/remove pending hierarchies") &&
        Expect(absent && once && notUpdatedDuringFlush && survivorUpdates == 2,
            "reentrant destruction should destroy once, keep unrelated additions, and defer their first update");
}
}

bool RunScenePendingRemovalTests()
{
    return ExistingParentIncludesPendingAndLateChildren() && PendingParentRemovesMixedDescendants() &&
        DestructionCanRemoveAndCreateObjects(false) && DestructionCanRemoveAndCreateObjects(true);
}

static const TestSupport::Registration gScenePendingRemovalTests{
    "RuntimeObject", "scene pending hierarchy removal should pass", RunScenePendingRemovalTests };
