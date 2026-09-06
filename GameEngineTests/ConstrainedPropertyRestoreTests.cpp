#include "ConstrainedPropertyRestoreTests.h"

#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

#include "Runtime/AudioSource.h"
#include "Runtime/Camera.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/PropertyDescriptor.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Serialization/SceneSerializer.h"
#include "TestSupport.h"

namespace
{
namespace Runtime = GameEngine::Runtime;
namespace Serialization = GameEngine::Serialization;

bool SameRanges(const Runtime::GameObject& first, const Runtime::GameObject& second)
{
    const auto* firstAudio = first.GetComponent<Runtime::AudioSource>();
    const auto* secondAudio = second.GetComponent<Runtime::AudioSource>();
    const auto* firstCamera = first.GetComponent<Runtime::Camera>();
    const auto* secondCamera = second.GetComponent<Runtime::Camera>();
    return firstAudio && secondAudio && firstCamera && secondCamera &&
        firstAudio->GetMinDistance() == secondAudio->GetMinDistance() &&
        firstAudio->GetMaxDistance() == secondAudio->GetMaxDistance() &&
        firstCamera->GetNearClipPlane() == secondCamera->GetNearClipPlane() &&
        firstCamera->GetFarClipPlane() == secondCamera->GetFarClipPlane();
}
}

bool RunConstrainedPropertyRestoreTests()
{
    const TestSupport::RegistryScope registrations;
    static_cast<void>(Serialization::RegisterRuntimeComponentFactories());
    bool passed = true;
    for (const bool aboveDefaults : { false, true })
    {
        Runtime::ObjectRegistry registry;
        Runtime::Input input;
        Runtime::RuntimeContext context(registry, input);
        Runtime::Scene scene(context, "Ranges");
        auto* object = scene.CreateGameObject("Ranged");
        auto* audio = object->AddComponent<Runtime::AudioSource>();
        auto* camera = object->AddComponent<Runtime::Camera>();
        if (aboveDefaults)
        {
            audio->SetMaxDistance(30.0f);
            audio->SetMinDistance(20.0f);
            camera->SetFarClipPlane(3000.0f);
            camera->SetNearClipPlane(2000.0f);
        }
        else
        {
            audio->SetMinDistance(0.1f);
            audio->SetMaxDistance(0.5f);
            camera->SetNearClipPlane(0.002f);
            camera->SetFarClipPlane(0.01f);
        }
        const auto cloned = object->Clone();
        const std::string text = Serialization::SceneSerializer::SaveToText(scene);
        const auto loaded = Serialization::SceneSerializer::LoadFromBytes(
            std::as_bytes(std::span(text.data(), text.size())), "Ranges.scene", context);
        const auto* loadedObject = loaded ? loaded->FindGameObject("Ranged") : nullptr;
        passed = TestSupport::Expect(cloned && SameRanges(*object, *cloned) &&
            loadedObject && SameRanges(*object, *loadedObject),
            "clone and scene loading must preserve both endpoints above and below default ranges") && passed;
    }

    Runtime::AudioSource audio;
    try
    {
        const Runtime::PropertyRestoreScope outer(audio);
        {
            const Runtime::PropertyRestoreScope inner(audio);
            audio.SetMinDistance(50.0f);
        }
        audio.SetMaxDistance(60.0f);
        throw std::runtime_error("interrupted restoration");
    }
    catch (const std::runtime_error&) {}
    audio.SetMinDistance(100.0f);
    passed = TestSupport::Expect(audio.GetMinDistance() == 60.0f && audio.GetMaxDistance() == 60.0f,
        "nested or interrupted restoration must restore the normal setter constraint") && passed;
    {
        const Runtime::PropertyRestoreScope restore(audio);
        audio.SetMinDistance(20.0f);
        audio.SetMaxDistance(10.0f);
    }
    return TestSupport::Expect(audio.GetMinDistance() <= audio.GetMaxDistance(),
        "invalid endpoint pairs must be normalized before the restored component becomes observable") && passed;
}

static const TestSupport::Registration gConstrainedPropertyRestoreTests{
    "RuntimeObject", "constrained property snapshots should restore without data loss", RunConstrainedPropertyRestoreTests };
