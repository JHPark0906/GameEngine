#include "SceneShutdownTests.h"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

#include "Runtime/ComponentType.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/MonoBehaviour.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"
#include "TestSupport.h"

namespace
{
namespace Runtime = GameEngine::Runtime;
using TestSupport::Expect;

class ShutdownProbe final : public Runtime::MonoBehaviour
{
public:
    static const Runtime::ComponentType& StaticType()
    {
        static const Runtime::ComponentType type{ "SceneShutdownProbe", &Runtime::MonoBehaviour::StaticType() };
        return type;
    }
    const Runtime::ComponentType& GetComponentType() const override { return StaticType(); }
    std::function<void(ShutdownProbe&)> destroy;

protected:
    void OnDestroy() override
    {
        if (destroy) destroy(*this);
    }
};

struct Fixture
{
    Runtime::ObjectRegistry registry;
    Runtime::Input input;
    Runtime::RuntimeContext context{ registry, input };
};

bool SceneCallbacksSeeDetachedObjects()
{
    Fixture fixture;
    std::array<unsigned int, 3> ids{};
    std::array<int, 3> destroyed{};
    bool callbacksSafe = true;
    auto scene = std::make_unique<Runtime::Scene>(fixture.context, "Shutdown");
    auto* const capturedScene = scene.get();
    Runtime::GameObject* parent = nullptr;
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        auto* object = scene->CreateGameObject("Object");
        ids[index] = object->GetInstanceId();
        if (parent) static_cast<void>(object->GetTransform().SetParent(&parent->GetTransform()));
        else parent = object;
        object->AddComponent<ShutdownProbe>()->destroy = [&, index](ShutdownProbe& probe)
        {
            ++destroyed[index];
            const auto& constScene = *capturedScene;
            callbacksSafe = !probe.GetScene() && capturedScene->GetGameObjects().empty() &&
                capturedScene->GetObjects().empty() && constScene.GetObjects().empty() &&
                capturedScene->GetRootGameObjects().empty() && constScene.GetRootGameObjects().empty() &&
                !capturedScene->FindGameObject("Object") && callbacksSafe;
            for (const auto id : ids)
            {
                callbacksSafe = !capturedScene->GetGameObject(id) && !capturedScene->FindObject(id) &&
                    !capturedScene->RemoveGameObject(id) && callbacksSafe;
            }
            callbacksSafe = !capturedScene->CreateGameObject("Rejected") && callbacksSafe;
            auto rejected = std::make_unique<Runtime::GameObject>(fixture.context, "Rejected owner");
            const auto rejectedId = rejected->GetInstanceId();
            callbacksSafe = !capturedScene->AddGameObject(std::move(rejected)) &&
                !fixture.registry.FindObject(rejectedId) && callbacksSafe;
            capturedScene->Update(0.0f);
        };
    }
    scene.reset();
    bool unregistered = true;
    for (const auto id : ids) unregistered = !fixture.registry.FindObject(id) && unregistered;
    return Expect(callbacksSafe && destroyed == std::array<int, 3>{ 1, 1, 1 } && unregistered,
        "scene shutdown must detach every object before callbacks and safely reject reentrant mutations");
}

bool ManagerCallbacksSeeEmptyManager()
{
    Fixture fixture;
    std::array<unsigned int, 2> ids{};
    int destroyed = 0;
    bool callbacksSafe = true;
    auto manager = std::make_unique<Runtime::SceneManager>(fixture.context);
    auto* const capturedManager = manager.get();
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        auto scene = std::make_unique<Runtime::Scene>(fixture.context, "Managed shutdown");
        scene->CreateGameObject("Probe")->AddComponent<ShutdownProbe>()->destroy = [&](ShutdownProbe& probe)
        {
            ++destroyed;
            callbacksSafe = !probe.GetScene() && capturedManager->GetActiveScenes().empty() && callbacksSafe;
            for (const auto id : ids)
            {
                callbacksSafe = !capturedManager->GetScene(id) && !capturedManager->IsSceneLoaded(id) &&
                    !capturedManager->UnloadScene(id) && callbacksSafe;
            }
            callbacksSafe = capturedManager->AddScene(
                std::make_unique<Runtime::Scene>(fixture.context, "Rejected")) == 0 && callbacksSafe;
            callbacksSafe = capturedManager->LoadScene(1) == Runtime::SceneLoadResult::Failed && callbacksSafe;
            capturedManager->Update(0.0f);
        };
        ids[index] = manager->AddScene(std::move(scene));
    }
    manager.reset();
    return Expect(callbacksSafe && destroyed == 2,
        "manager shutdown must remove all scenes from lookup before destruction callbacks");
}

bool UnloadCallbacksCanChangeOtherScenes()
{
    Fixture fixture;
    int destroyed = 0;
    unsigned int firstId = 0;
    unsigned int secondId = 0;
    unsigned int survivorId = 0;
    bool callbacksSafe = false;
    Runtime::SceneManager manager(fixture.context);
    auto first = std::make_unique<Runtime::Scene>(fixture.context, "First");
    auto second = std::make_unique<Runtime::Scene>(fixture.context, "Second");
    first->CreateGameObject("First probe")->AddComponent<ShutdownProbe>()->destroy = [&](ShutdownProbe&)
    {
        ++destroyed;
        callbacksSafe = !manager.GetScene(firstId) && !manager.UnloadScene(firstId) &&
            manager.UnloadScene(secondId);
        survivorId = manager.AddScene(std::make_unique<Runtime::Scene>(fixture.context, "Survivor"));
    };
    second->CreateGameObject("Second probe")->AddComponent<ShutdownProbe>()->destroy = [&](ShutdownProbe&)
    {
        ++destroyed;
    };
    firstId = manager.AddScene(std::move(first));
    secondId = manager.AddScene(std::move(second));
    const bool removed = manager.UnloadScene(firstId);
    return Expect(removed && callbacksSafe && destroyed == 2 && survivorId != 0 &&
        manager.GetScene(survivorId) && manager.GetActiveScenes().size() == 1,
        "ordinary unload must permit callback removal/addition without retaining invalid map iterators");
}
}

bool RunSceneShutdownTests()
{
    return SceneCallbacksSeeDetachedObjects() && ManagerCallbacksSeeEmptyManager() &&
        UnloadCallbacksCanChangeOtherScenes();
}

static const TestSupport::Registration gSceneShutdownTests{
    "RuntimeObject", "scene and manager shutdown callbacks should be safe", RunSceneShutdownTests };
