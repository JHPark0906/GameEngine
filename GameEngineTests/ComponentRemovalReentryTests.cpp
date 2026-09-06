#include "ComponentRemovalReentryTests.h"

#include <functional>
#include <utility>

#include "Runtime/BoxCollider2D.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/MonoBehaviour.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RuntimeContext.h"
#include "TestSupport.h"

namespace
{
namespace Runtime = GameEngine::Runtime;

class RemovalProbe final : public Runtime::MonoBehaviour
{
public:
    static const Runtime::ComponentType& StaticType()
    {
        static const Runtime::ComponentType type{ "RemovalReentryProbe", &Runtime::MonoBehaviour::StaticType() };
        return type;
    }
    const Runtime::ComponentType& GetComponentType() const override { return StaticType(); }
    std::function<void()> update;
    std::function<void()> destroy;

private:
    void Update(float) override
    {
        const auto action = std::exchange(update, {});
        if (action) action();
    }
    void OnDestroy() override { if (destroy) destroy(); }
};
}

bool RunComponentRemovalReentryTests()
{
    bool passed = true;
    for (const bool deferred : { false, true })
    {
        Runtime::ObjectRegistry registry;
        Runtime::Input input;
        Runtime::RuntimeContext context(registry, input);
        Runtime::GameObject object(context);
        auto* driver = object.AddComponent<RemovalProbe>();
        auto* removed = object.AddComponent<RemovalProbe>();
        auto* sibling = object.AddComponent<RemovalProbe>();
        const unsigned int removedId = removed->GetInstanceId();
        const unsigned int siblingId = sibling->GetInstanceId();
        int destroyed = 0;
        bool callbacksValid = true;
        sibling->destroy = [&] { ++destroyed; };
        removed->destroy = [&]
        {
            ++destroyed;
            callbacksValid = removed->GetGameObject() == &object && removed->GetTransform() != nullptr;
            // Exceed any plausible initial vector capacity while the removal callback is executing.
            for (int index = 0; index < 64; ++index)
                callbacksValid = object.AddComponent<Runtime::BoxCollider2D>() != nullptr && callbacksValid;
            callbacksValid = object.RemoveComponent(sibling) && callbacksValid;
        };
        if (deferred)
        {
            driver->update = [&] { callbacksValid = object.RemoveComponent(removed) && callbacksValid; };
            object.Update(0.016f);
        }
        else
        {
            callbacksValid = object.RemoveComponent(removed) && callbacksValid;
        }
        passed = TestSupport::Expect(callbacksValid && destroyed == 2 &&
            registry.FindObject(removedId) == nullptr && registry.FindObject(siblingId) == nullptr &&
            object.GetAllComponents().size() == 66,
            "component destruction may grow its owner's component list and remove a sibling exactly once") && passed;
    }
    return passed;
}

static const TestSupport::Registration gComponentRemovalReentryTests{
    "RuntimeObject", "component removal callback reentry should remain safe", RunComponentRemovalReentryTests };
