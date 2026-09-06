#include "ListenerRequiredTests.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>
#include <vector>

#include "Assets/AssetDatabase.h"
#include "Assets/AssetReference.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Runtime/AudioListener.h"
#include "Runtime/AudioSource.h"
#include "Runtime/AudioSystem.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using TestSupport::Expect;

    template <typename T>
    void WriteValue(std::ofstream& stream, const T value)
    {
        stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    bool WriteHalfSecondClip(const std::filesystem::path& path)
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write("RIFF", 4);
        WriteValue<std::uint32_t>(stream, 2036);
        stream.write("WAVEfmt ", 8);
        WriteValue<std::uint32_t>(stream, 16);
        WriteValue<std::uint16_t>(stream, 1);
        WriteValue<std::uint16_t>(stream, 1);
        WriteValue<std::uint32_t>(stream, 2000);
        WriteValue<std::uint32_t>(stream, 4000);
        WriteValue<std::uint16_t>(stream, 2);
        WriteValue<std::uint16_t>(stream, 16);
        stream.write("data", 4);
        WriteValue<std::uint32_t>(stream, 2000);
        const std::vector<std::int16_t> samples(1000, 1000);
        stream.write(reinterpret_cast<const char*>(samples.data()), 2000);
        return static_cast<bool>(stream);
    }

    /// <summary>볼륨과 독립적으로 전진하는 출력 커서다. 리스너 변화가 보이스를 재시작하면 드러난다.</summary>
    class ClockedOutput final : public Platform::IAudioOutput
    {
    public:
        bool Initialize() override { return true; }
        std::uint64_t CreateVoice(const Platform::AudioVoiceDescription& description) override
        {
            ++creates;
            cursor = 0;
            totalFrames = description.samples.size() / description.channelCount;
            return 1;
        }
        void StartVoice(std::uint64_t) override { ++starts; advancing = true; }
        void StopVoice(std::uint64_t) override { ++stops; advancing = false; }
        void SetVoiceVolume(std::uint64_t, const float newVolume) override { volume = newVolume; }
        bool IsVoiceFinished(std::uint64_t) override { return cursor >= totalFrames; }
        void DestroyVoice(std::uint64_t) override { ++destroys; advancing = false; }
        void Advance(const std::size_t frames) { if (advancing) cursor += frames; }

        int creates = 0;
        int starts = 0;
        int stops = 0;
        int destroys = 0;
        float volume = -1.0f;
        std::size_t cursor = 0;
        std::size_t totalFrames = 0;
        bool advancing = false;
    };
}

bool RunListenerRequiredTests()
{
    using namespace GameEngine;
    using TestSupport::Expect;
    TestSupport::TemporaryDirectory directory{ "listener-required" };
    if (!Expect(WriteHalfSecondClip(directory.GetPath() / "sound.wav") &&
        TestSupport::WriteFile(directory.GetPath() / "Listener.gameproject", "{}"),
        "listener policy fixtures should be written")) return false;
    Platform::DirectoryContentSource content(directory.GetPath());
    Assets::AssetDatabase assets;
    if (!Expect(assets.Refresh(content), "listener policy assets should load")) return false;

    Runtime::ObjectRegistry objects;
    Runtime::Input input;
    Runtime::RuntimeContext context{ objects, input };
    Runtime::SceneManager scenes{ context };
    auto ownedScene = std::make_unique<Runtime::Scene>(context, "Listener required");
    Runtime::Scene* const scene = ownedScene.get();
    auto* source = scene->CreateGameObject("Source")->AddComponent<Runtime::AudioSource>();
    source->SetClip(Assets::AssetReference("sound.wav"));
    source->SetVolume(0.8f);
    source->SetSpatialBlend(0.0f);
    if (!Expect(scenes.AddScene(std::move(ownedScene)) != 0, "listener scene should activate"))
        return false;
    auto ownedOutput = std::make_unique<ClockedOutput>();
    ClockedOutput* const output = ownedOutput.get();
    Runtime::AudioSystem system(std::move(ownedOutput));

    bool passed = Expect(source->Play(), "the source should start without a listener");
    system.Synchronize(scenes, assets);
    passed &= Expect(output->creates == 1 && output->starts == 1 && output->volume == 0.0f &&
        source->IsPlaying() && source->GetClipDurationSeconds() == 0.5,
        "no listener must mute spatialBlend zero while starting the voice and resolving duration");
    output->Advance(100);
    system.Synchronize(scenes, assets);

    auto* listenerParent = scene->CreateGameObject("Listener parent");
    auto* listenerObject = scene->CreateGameObject("Late listener");
    static_cast<void>(listenerObject->GetTransform().SetParent(&listenerParent->GetTransform()));
    listenerObject->GetTransform().SetPosition({ 1000.0f, 0.0f, 0.0f });
    auto* listener = listenerObject->AddComponent<Runtime::AudioListener>();
    system.Synchronize(scenes, assets);
    passed &= Expect(output->volume == 0.8f && output->creates == 1 && output->starts == 1 &&
        output->cursor == 100,
        "a listener attached later must reveal the ongoing non-spatial sound without restarting");

    listener->SetEnabled(false);
    system.Synchronize(scenes, assets);
    passed &= Expect(output->volume == 0.0f, "a disabled only listener must mute the output");
    output->Advance(100);
    listener->SetEnabled(true);
    listenerParent->SetActive(false);
    system.Synchronize(scenes, assets);
    passed &= Expect(output->volume == 0.0f,
        "an enabled listener beneath an inactive parent must still leave output muted");
    output->Advance(100);
    listenerParent->SetActive(true);
    system.Synchronize(scenes, assets);
    passed &= Expect(output->volume == 0.8f && output->cursor == 300 && output->stops == 0,
        "reenabling the listener hierarchy must resume audibility at the advanced cursor");

    auto* second = scene->CreateGameObject("Near listener")->AddComponent<Runtime::AudioListener>();
    source->SetSpatialBlend(1.0f);
    source->SetMinDistance(1.0f);
    source->SetMaxDistance(11.0f);
    system.Synchronize(scenes, assets);
    passed &= Expect(listener->GetInstanceId() < second->GetInstanceId() && output->volume == 0.0f,
        "the lowest active listener ID must win even when a newer listener is closer");
    listener->SetEnabled(false);
    system.Synchronize(scenes, assets);
    passed &= Expect(output->volume == 0.8f,
        "disabling the winning listener must select the next active listener");
    second->SetEnabled(false);
    system.Synchronize(scenes, assets);
    passed &= Expect(output->volume == 0.0f && source->IsPlaying(),
        "disabling all listeners must mute the remaining playback");
    output->Advance(700);
    system.Synchronize(scenes, assets);
    passed &= Expect(!source->IsPlaying() && output->creates == 1 && output->starts == 1 &&
        output->stops == 0 && output->destroys == 1 && source->GetClipDurationSeconds() == 0.5,
        "muted playback must naturally finish and keep clip duration without restarting or pausing");
    return passed;
}

static const TestSupport::Registration gListenerRequiredTests{
    "Audio", "audio requires an active listener without pausing playback", RunListenerRequiredTests };
