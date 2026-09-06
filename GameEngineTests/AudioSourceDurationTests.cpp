#include "AudioSourceDurationTests.h"

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Assets/AssetDatabase.h"
#include "Assets/AssetReference.h"
#include "Assets/AudioData.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Runtime/AudioSource.h"
#include "Runtime/AudioSystem.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Serialization/SceneSerializer.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using TestSupport::Expect;

    template <typename T>
    concept PublicDurationSetter = requires(T& source) { source.SetClipDurationSeconds(1.0); };

    static_assert(std::same_as<decltype(std::declval<const Runtime::AudioSource&>()
        .GetClipDurationSeconds()), double>);
    static_assert(!PublicDurationSetter<Runtime::AudioSource>);

    constexpr double MonoDuration = 10001.0 / 8000.0;
    constexpr double StereoDuration = 15005.0 / 11025.0;

    bool Near(const double left, const double right)
    {
        return std::abs(left - right) < 1.0e-12;
    }

    template <typename T>
    void WriteValue(std::ofstream& stream, const T value)
    {
        stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    bool WriteWav(const std::filesystem::path& path, const std::uint16_t channels,
        const std::uint32_t sampleRate, const std::uint32_t frames)
    {
        std::ofstream stream(path, std::ios::binary);
        const std::uint32_t dataBytes = frames * channels * 2;
        stream.write("RIFF", 4);
        WriteValue<std::uint32_t>(stream, 36 + dataBytes);
        stream.write("WAVEfmt ", 8);
        WriteValue<std::uint32_t>(stream, 16);
        WriteValue<std::uint16_t>(stream, 1);
        WriteValue<std::uint16_t>(stream, channels);
        WriteValue<std::uint32_t>(stream, sampleRate);
        WriteValue<std::uint32_t>(stream, sampleRate * channels * 2);
        WriteValue<std::uint16_t>(stream, static_cast<std::uint16_t>(channels * 2));
        WriteValue<std::uint16_t>(stream, 16);
        stream.write("data", 4);
        WriteValue<std::uint32_t>(stream, dataBytes);
        const std::vector<std::int16_t> samples(static_cast<std::size_t>(frames) * channels, 1000);
        stream.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
        return static_cast<bool>(stream);
    }

    class ProbeOutput final : public Platform::IAudioOutput
    {
    public:
        enum class Mode { Ready, NoDevice, NoVoice };
        explicit ProbeOutput(const Mode mode) : mMode(mode) {}

        bool Initialize() override { ++initializeCalls; return mMode != Mode::NoDevice; }
        std::uint64_t CreateVoice(const Platform::AudioVoiceDescription& description) override
        {
            ++createCalls;
            lastChannelCount = description.channelCount;
            return mMode == Mode::NoVoice ? 0 : static_cast<std::uint64_t>(createCalls);
        }
        void StartVoice(std::uint64_t) override {}
        void StopVoice(std::uint64_t) override {}
        void SetVoiceVolume(std::uint64_t, float) override {}
        bool IsVoiceFinished(std::uint64_t) override { return false; }
        void DestroyVoice(std::uint64_t) override { ++destroyCalls; }

        int initializeCalls = 0;
        int createCalls = 0;
        int destroyCalls = 0;
        std::uint32_t lastChannelCount = 0;

    private:
        Mode mMode;
    };

    struct Stage
    {
        TestSupport::TemporaryDirectory directory{ "audio-source-duration" };
        Platform::DirectoryContentSource content{ directory.GetPath() };
        Assets::AssetDatabase assets;
        Runtime::ObjectRegistry objects;
        Runtime::Input input;
        Runtime::RuntimeContext context{ objects, input };
        Runtime::SceneManager scenes{ context };
        Runtime::AudioSource* source = nullptr;

        bool Prepare()
        {
            if (!WriteWav(directory.GetPath() / "mono.wav", 1, 8000, 10001) ||
                !WriteWav(directory.GetPath() / "stereo.wav", 2, 11025, 15005) ||
                !TestSupport::WriteFile(directory.GetPath() / "Duration.gameproject", "{}") ||
                !assets.Refresh(content))
            {
                return false;
            }
            auto scene = std::make_unique<Runtime::Scene>(context, "Audio duration");
            source = scene->CreateGameObject("Source")->AddComponent<Runtime::AudioSource>();
            source->SetClip(Assets::AssetReference("mono.wav"));
            return scenes.AddScene(std::move(scene)) != 0;
        }
    };

    bool CheckSourceDurations(std::unique_ptr<ProbeOutput> output, const bool silent)
    {
        Stage stage;
        if (!Expect(stage.Prepare(), "duration PCM fixtures should load")) return false;
        ProbeOutput* const probe = output.get();
        Runtime::AudioSystem system(std::move(output));
        Runtime::AudioSource& source = *stage.source;
        bool passed = Expect(source.GetClipDurationSeconds() == 0.0,
            "a clip that has not been decoded must have unknown duration");
        system.Synchronize(stage.scenes, stage.assets);
        passed &= Expect(source.GetClipDurationSeconds() == 0.0 &&
            stage.assets.GetLoadedPayloadCount() == 0,
            "a stopped source must not decode its clip just to measure duration");

        source.SetLooping(true);
        source.SetPitch(2.0f);
        passed &= Expect(source.Play(), "the mono clip should accept Play");
        system.Synchronize(stage.scenes, stage.assets);
        passed &= Expect(Near(source.GetClipDurationSeconds(), MonoDuration),
            "PCM duration must retain fractional seconds and ignore looping or pitch");
        const auto json = Serialization::SceneSerializer::SaveComponentToJson(source);
        passed &= Expect(json && !json->Find("clipDurationSeconds"),
            "decoded duration is a read-only runtime observation, not authored scene state");
        source.SetClip(Assets::AssetReference("mono.wav"));
        passed &= Expect(Near(source.GetClipDurationSeconds(), MonoDuration),
            "assigning the same clip must preserve its known duration");

        if (silent)
        {
            // Refresh drops the database's resident table. An unchanged silent playback should
            // retain its decoded observation and avoid resolving the clip again each frame.
            passed &= Expect(stage.assets.Refresh(stage.content), "the asset table should refresh");
            system.Synchronize(stage.scenes, stage.assets);
            system.Synchronize(stage.scenes, stage.assets);
            passed &= Expect(stage.assets.GetLoadedPayloadCount() == 0 &&
                Near(source.GetClipDurationSeconds(), MonoDuration),
                "silent playback must not reload its unchanged clip on every synchronization");
        }

        passed &= Expect(source.Pause(), "the source should pause before replacing its clip");
        system.Synchronize(stage.scenes, stage.assets);
        const int previousCreates = probe ? probe->createCalls : 0;
        const int previousDestroys = probe ? probe->destroyCalls : 0;
        source.SetClip(Assets::AssetReference("stereo.wav"));
        passed &= Expect(source.GetClipDurationSeconds() == 0.0,
            "a different clip must immediately invalidate duration");
        system.Synchronize(stage.scenes, stage.assets);
        passed &= Expect(Near(source.GetClipDurationSeconds(), StereoDuration) &&
            source.GetPlaybackState() == Runtime::AudioSource::PlaybackState::Paused,
            "a paused clip replacement must resolve its own stereo PCM duration");
        if (probe && !silent)
        {
            passed &= Expect(probe->createCalls == previousCreates + 1 &&
                probe->destroyCalls == previousDestroys + 1 && probe->lastChannelCount == 2,
                "replacing a paused clip must replace the old voice as well as its duration");
        }

        source.Stop();
        system.Synchronize(stage.scenes, stage.assets);
        passed &= Expect(Near(source.GetClipDurationSeconds(), StereoDuration),
            "stopping playback must preserve the duration of the same clip");
        source.SetClip(Assets::AssetReference("missing.wav"));
        passed &= Expect(source.GetClipDurationSeconds() == 0.0 && source.Play(),
            "a missing replacement begins unknown before its load attempt");
        system.Synchronize(stage.scenes, stage.assets);
        system.Synchronize(stage.scenes, stage.assets);
        passed &= Expect(source.GetClipDurationSeconds() == 0.0,
            "a failed clip load must leave duration unknown");

        // Repairing the clip reference itself must invalidate the failed slot, even without Play.
        source.SetClip(Assets::AssetReference("mono.wav"));
        system.Synchronize(stage.scenes, stage.assets);
        passed &= Expect(Near(source.GetClipDurationSeconds(), MonoDuration),
            "a corrected clip must resolve after an earlier failure without another Play call");
        if (probe)
        {
            passed &= Expect(probe->initializeCalls == 1,
                "an unavailable output must be tried once while clip duration remains available");
        }
        return passed;
    }
}

bool RunAudioSourceDurationTests()
{
    using namespace GameEngine;
    Assets::AudioData invalid;
    bool passed = Expect(invalid.GetDurationSeconds() == 0.0,
        "invalid decoded audio must have zero duration without division by zero");
    passed &= CheckSourceDurations(nullptr, true);
    passed &= CheckSourceDurations(std::make_unique<ProbeOutput>(ProbeOutput::Mode::NoDevice), true);
    passed &= CheckSourceDurations(std::make_unique<ProbeOutput>(ProbeOutput::Mode::Ready), false);

    Stage stage;
    if (!Expect(stage.Prepare(), "voice failure duration fixture should load")) return false;
    auto output = std::make_unique<ProbeOutput>(ProbeOutput::Mode::NoVoice);
    ProbeOutput* const probe = output.get();
    Runtime::AudioSystem system(std::move(output));
    static_cast<void>(stage.source->Play());
    system.Synchronize(stage.scenes, stage.assets);
    system.Synchronize(stage.scenes, stage.assets);
    passed &= Expect(Near(stage.source->GetClipDurationSeconds(), MonoDuration) &&
        probe->createCalls == 1,
        "voice creation failure must retain decoded duration without retrying every frame");
    return passed;
}

static const TestSupport::Registration gAudioSourceDurationTests{
    "Audio", "audio source clip duration tests should pass", RunAudioSourceDurationTests };
