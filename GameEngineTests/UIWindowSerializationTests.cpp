#include "UIWindowSerializationTests.h"

#include <memory>
#include <span>
#include <string>
#include <utility>

#include "Runtime/Button.h"
#include "Runtime/Canvas.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RectTransform.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"
#include "Runtime/UIEventSystem.h"
#include "Runtime/UILayoutSystem.h"
#include "Runtime/UIWindow.h"
#include "Serialization/ComponentFactory.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Serialization/SceneSerializer.h"
#include "TestSupport.h"

bool RunUIWindowSerializationTests()
{
    using namespace GameEngine;
    TestSupport::RegistryScope registrations;
    static_cast<void>(Serialization::RegisterRuntimeComponentFactories());
    Runtime::ObjectRegistry registry;
    Runtime::Input input;
    Runtime::RuntimeContext context{ registry, input };
    Runtime::Scene source(context, "Window roundtrip");
    auto* canvas = source.CreateGameObject("Canvas");
    static_cast<void>(canvas->AddComponent<Runtime::Canvas>());
    auto* lower = source.CreateGameObject("Lower button");
    static_cast<void>(lower->GetTransform().SetParent(&canvas->GetTransform()));
    lower->AddComponent<Runtime::RectTransform>()->SetOffsetMax({ 100.0f, 100.0f });
    static_cast<void>(lower->AddComponent<Runtime::Button>());
    auto* upper = source.CreateGameObject("Upper window");
    static_cast<void>(upper->GetTransform().SetParent(&canvas->GetTransform()));
    upper->AddComponent<Runtime::RectTransform>()->SetOffsetMax({ 100.0f, 100.0f });
    upper->AddComponent<Runtime::UIWindow>()->SetModal(true);
    const std::string text = Serialization::SceneSerializer::SaveToText(source);
    auto restored = Serialization::SceneSerializer::LoadFromBytes(
        std::as_bytes(std::span(text.data(), text.size())), "Window.scene", context);
    if (!TestSupport::Expect(restored && Serialization::ComponentFactory::IsRegistered("UIWindow"),
        "UIWindow must have an engine factory and restore through the full scene loader")) return false;
    auto* restoredUpper = restored->FindGameObject("Upper window");
    auto* restoredLower = restored->FindGameObject("Lower button");
    auto* window = restoredUpper ? restoredUpper->GetComponent<Runtime::UIWindow>() : nullptr;
    auto* button = restoredLower ? restoredLower->GetComponent<Runtime::Button>() : nullptr;
    if (!TestSupport::Expect(window && button && !window->IsModal(),
        "a saved UIWindow must restore as its concrete type with transient modal state cleared")) return false;
    Runtime::SceneManager scenes(context);
    static_cast<void>(scenes.AddScene(std::move(restored)));
    return TestSupport::ForEachUiScale([&](const float scale)
    {
        scenes.GetActiveScenes().begin()->second->FindGameObject("Canvas")
            ->GetComponent<Runtime::Canvas>()->SetScaleFactor(scale);
        Runtime::UILayoutSystem{}.Synchronize(scenes, 640.0f, 480.0f);
        Runtime::UIEventSystem events;
        static_cast<void>(events.Synchronize(scenes, input));
        return TestSupport::Expect(!button->IsHovered(),
            "a restored window background must still occlude lower controls at either UI scale");
    });
}

static const TestSupport::Registration gUIWindowSerializationTests{
    "RuntimeObject", "UIWindow scene roundtrip should preserve window behavior", RunUIWindowSerializationTests };
