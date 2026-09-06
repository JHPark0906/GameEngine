#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../GameEngine/Assets/AssetDatabase.h"
#include "../GameEngine/Assets/AssetReference.h"
#include "../GameEngine/Assets/WavAudio.h"
#include "../GameEngine/Math/Vector.h"
#include "../GameEngine/Platform/DirectoryContentSource.h"
#include "../GameEngine/Platform/IAudioOutput.h"
#include "../GameEngine/Runtime/AudioListener.h"
#include "../GameEngine/Runtime/AudioSource.h"
#include "../GameEngine/Runtime/AudioSystem.h"
#include "../GameEngine/Runtime/GameObject.h"
#include "../GameEngine/Runtime/Input.h"
#include "../GameEngine/Runtime/ObjectRegistry.h"
#include "../GameEngine/Runtime/RuntimeContext.h"
#include "../GameEngine/Runtime/Scene.h"
#include "../GameEngine/Runtime/SceneManager.h"
#include "../GameEngine/Runtime/Transform.h"

#include "AudioTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    void AppendBytes(std::vector<std::byte>& bytes, const void* data, const std::size_t size)
    {
        const auto* const raw = static_cast<const std::byte*>(data);
        bytes.insert(bytes.end(), raw, raw + size);
    }

    template <typename T>
    void AppendValue(std::vector<std::byte>& bytes, const T value)
    {
        AppendBytes(bytes, &value, sizeof(T));
    }

    /// <summary>테스트가 쓰는 최소 WAV다. 헤더 값을 바꿔 지원 경계 사례를 만든다.</summary>
    [[nodiscard]] std::vector<std::byte> MakeWavBytes(
        const std::uint16_t formatTag, const std::uint16_t channelCount,
        const std::uint32_t sampleRate, const std::uint16_t bitsPerSample,
        const std::span<const std::int16_t> samples)
    {
        const std::uint32_t dataSize = static_cast<std::uint32_t>(samples.size_bytes());
        std::vector<std::byte> bytes;
        AppendBytes(bytes, "RIFF", 4);
        AppendValue<std::uint32_t>(bytes, 36 + dataSize);
        AppendBytes(bytes, "WAVE", 4);
        AppendBytes(bytes, "fmt ", 4);
        AppendValue<std::uint32_t>(bytes, 16);
        AppendValue<std::uint16_t>(bytes, formatTag);
        AppendValue<std::uint16_t>(bytes, channelCount);
        AppendValue<std::uint32_t>(bytes, sampleRate);
        AppendValue<std::uint32_t>(bytes, sampleRate * channelCount * 2u);
        AppendValue<std::uint16_t>(bytes, static_cast<std::uint16_t>(channelCount * 2));
        AppendValue<std::uint16_t>(bytes, bitsPerSample);
        AppendBytes(bytes, "data", 4);
        AppendValue<std::uint32_t>(bytes, dataSize);
        AppendBytes(bytes, samples.data(), samples.size_bytes());
        return bytes;
    }

    bool RunWavDecodeTests()
    {
        using GameEngine::Assets::DecodedWavAudio;
        using GameEngine::Assets::DecodeWavAudio;

        const std::int16_t monoSamples[] = { 0, 1000, -1000, 32767 };
        const std::vector<std::byte> mono = MakeWavBytes(1, 1, 44100, 16, monoSamples);
        DecodedWavAudio decoded;
        std::string error;
        const bool monoDecodes = DecodeWavAudio(mono, decoded, error) &&
            decoded.channelCount == 1 && decoded.sampleRate == 44100 &&
            decoded.samples.size() == 4 && decoded.samples[3] == 32767;

        const std::int16_t stereoSamples[] = { 1, 2, 3, 4, 5, 6 };
        const std::vector<std::byte> stereo = MakeWavBytes(1, 2, 22050, 16, stereoSamples);
        DecodedWavAudio stereoDecoded;
        const bool stereoDecodes = DecodeWavAudio(stereo, stereoDecoded, error) &&
            stereoDecoded.channelCount == 2 && stereoDecoded.samples.size() == 6;

        // 모르는 청크는 건너뛴다: fmt와 data 사이에 LIST가 끼어 있어도 읽힌다.
        std::vector<std::byte> withExtraChunk = MakeWavBytes(1, 1, 8000, 16, monoSamples);
        std::vector<std::byte> listChunk;
        AppendBytes(listChunk, "LIST", 4);
        AppendValue<std::uint32_t>(listChunk, 4);
        AppendBytes(listChunk, "INFO", 4);
        withExtraChunk.insert(withExtraChunk.begin() + 36, listChunk.begin(), listChunk.end());
        DecodedWavAudio extraDecoded;
        const bool extraChunkSkipped = DecodeWavAudio(withExtraChunk, extraDecoded, error) &&
            extraDecoded.samples.size() == 4;

        return Expect(monoDecodes, "a 16-bit PCM mono WAV should decode") &&
            Expect(stereoDecodes, "a 16-bit PCM stereo WAV should decode") &&
            Expect(extraChunkSkipped, "unknown chunks between fmt and data should be skipped");
    }

    bool RunWavRejectionTests()
    {
        using GameEngine::Assets::DecodedWavAudio;
        using GameEngine::Assets::DecodeWavAudio;

        const std::int16_t samples[] = { 0, 0 };
        DecodedWavAudio decoded;
        std::string error;

        const bool floatRejected =
            !DecodeWavAudio(MakeWavBytes(3, 1, 44100, 16, samples), decoded, error) &&
            error.find("PCM") != std::string::npos;
        const bool depthRejected =
            !DecodeWavAudio(MakeWavBytes(1, 1, 44100, 8, samples), decoded, error) &&
            error.find("16-bit") != std::string::npos;
        const bool channelsRejected =
            !DecodeWavAudio(MakeWavBytes(1, 4, 44100, 16, samples), decoded, error) &&
            error.find("channels") != std::string::npos;

        const char notWav[] = "just some text";
        const bool notRiffRejected = !DecodeWavAudio(
            std::as_bytes(std::span(notWav, sizeof(notWav))), decoded, error);

        // 청크가 파일 끝을 지나치는 잘린 파일은 거절된다.
        std::vector<std::byte> truncated = MakeWavBytes(1, 1, 44100, 16, samples);
        truncated.resize(truncated.size() - 2);
        const bool truncatedRejected = !DecodeWavAudio(truncated, decoded, error);

        return Expect(floatRejected, "float WAV should be rejected naming PCM") &&
            Expect(depthRejected, "8-bit WAV should be rejected naming the depth") &&
            Expect(channelsRejected, "a 4-channel WAV should be rejected naming channels") &&
            Expect(notRiffRejected, "non-RIFF bytes should be rejected") &&
            Expect(truncatedRejected, "a truncated WAV should be rejected");
    }

    /// <summary>
    /// 시스템 오디오 장치 없이 AudioSystem을 시험하는 가짜 출력이다. 호출을 기록하고 살아 있는
    /// 보이스를 흉내 낸다.
    /// </summary>
    class FakeAudioOutput final : public GameEngine::Platform::IAudioOutput
    {
    public:
        struct Voice
        {
            std::uint32_t channelCount = 0;
            std::uint32_t sampleRate = 0;
            std::size_t sampleCount = 0;
            bool loop = false;
            bool started = false;
            float volume = -1.0f;
        };

        [[nodiscard]] bool Initialize() override
        {
            ++initializeCalls;
            return true;
        }

        [[nodiscard]] std::uint64_t CreateVoice(
            const GameEngine::Platform::AudioVoiceDescription& description) override
        {
            ++createCalls;
            const std::uint64_t voiceId = mNextVoiceId++;
            voices.emplace(voiceId, Voice{ description.channelCount, description.sampleRate,
                description.samples.size(), description.loop, false, -1.0f });
            return voiceId;
        }

        void StartVoice(const std::uint64_t voiceId) override { voices.at(voiceId).started = true; }
        void StopVoice(const std::uint64_t voiceId) override { voices.at(voiceId).started = false; }
        void SetVoiceVolume(const std::uint64_t voiceId, const float volume) override
        {
            voices.at(voiceId).volume = volume;
        }
        [[nodiscard]] bool HasFailed() override { return failed; }
        [[nodiscard]] bool IsVoiceFinished(const std::uint64_t voiceId) override
        {
            // 실제 출력이 그러듯, 무너진 뒤의 보이스는 전부 끝난 것으로 답한다. 진행하지 않는
            // 보이스를 "아직"이라고 말하면 소스가 영원히 Playing으로 남는다.
            return failed || finishedVoices.contains(voiceId);
        }
        void DestroyVoice(const std::uint64_t voiceId) override
        {
            ++destroyCalls;
            voices.erase(voiceId);
        }

        int initializeCalls = 0;
        int createCalls = 0;
        int destroyCalls = 0;
        /// <summary>재생 중에 출력 장치가 사라진 상태다.</summary>
        bool failed = false;
        std::unordered_map<std::uint64_t, Voice> voices;
        std::unordered_set<std::uint64_t> finishedVoices;

    private:
        std::uint64_t mNextVoiceId = 1;
    };

    /// <summary>장면 하나와 재생할 클립 하나가 있는 오디오 무대다.</summary>
    struct AudioStage
    {
        TestSupport::TemporaryDirectory directory{ "audio-system" };
        GameEngine::Platform::DirectoryContentSource source{ directory.GetPath() };
        GameEngine::Assets::AssetDatabase assetDatabase;
        GameEngine::Runtime::ObjectRegistry objectRegistry;
        GameEngine::Runtime::Input input;
        GameEngine::Runtime::RuntimeContext runtimeContext{ objectRegistry, input };
        GameEngine::Runtime::SceneManager sceneManager{ runtimeContext };

        unsigned int sceneId = 0;
        GameEngine::Runtime::GameObject* gameObject = nullptr;
        GameEngine::Runtime::AudioSource* audioSource = nullptr;

        [[nodiscard]] bool Prepare()
        {
            const std::int16_t samples[] = { 0, 5000, -5000, 0, 2500, -2500 };
            const std::vector<std::byte> wav = MakeWavBytes(1, 1, 44100, 16, samples);
            std::ofstream stream(directory.GetPath() / "beep.wav", std::ios::binary);
            stream.write(reinterpret_cast<const char*>(wav.data()),
                static_cast<std::streamsize>(wav.size()));
            stream.close();

            // 데이터베이스는 루트의 .gameproject 하나를 프로젝트의 표지로 요구한다.
            if (!TestSupport::WriteFile(directory.GetPath() / "AudioTest.gameproject", "{}"))
            {
                return false;
            }
            if (!assetDatabase.Refresh(source))
            {
                return false;
            }
            auto scene = std::make_unique<GameEngine::Runtime::Scene>(runtimeContext, "AudioScene");
            gameObject = scene->CreateGameObject("speaker");
            audioSource = gameObject ? gameObject->AddComponent<GameEngine::Runtime::AudioSource>()
                                     : nullptr;
            if (audioSource)
            {
                audioSource->SetClip(GameEngine::Assets::AssetReference("beep.wav"));
            }
            sceneId = sceneManager.AddScene(std::move(scene));
            return audioSource != nullptr && sceneId != 0;
        }

        /// <summary>주어진 월드 위치에 새 AudioListener를 붙인 GameObject를 무대의 장면에
        /// 더한다. 실패하면 null이다.</summary>
        [[nodiscard]] GameEngine::Runtime::AudioListener* AddListener(
            const GameEngine::Math::Vector3& position)
        {
            GameEngine::Runtime::Scene* const activeScene = sceneManager.GetScene(sceneId);
            GameEngine::Runtime::GameObject* const listenerObject =
                activeScene ? activeScene->CreateGameObject("listener") : nullptr;
            if (!listenerObject)
            {
                return nullptr;
            }
            listenerObject->GetTransform().SetPosition(position);
            return listenerObject->AddComponent<GameEngine::Runtime::AudioListener>();
        }
    };

    bool RunPlaybackLifetimeTests()
    {
        using GameEngine::Runtime::AudioSource;
        using GameEngine::Runtime::AudioSystem;

        AudioStage stage;
        if (!Expect(stage.Prepare(), "the audio stage should assemble"))
        {
            return false;
        }
        auto fakeOwned = std::make_unique<FakeAudioOutput>();
        FakeAudioOutput* const fake = fakeOwned.get();
        AudioSystem system(std::move(fakeOwned));

        // 재생: 보이스가 만들어지고, 시작되고, 클립의 모양과 반복 플래그를 그대로 나른다.
        if (!Expect(stage.AddListener({ 0.0f, 0.0f, 0.0f }) != nullptr,
            "playback volume tests need an active listener")) return false;
        stage.audioSource->SetLooping(true);
        stage.audioSource->SetVolume(0.5f);
        const bool played = stage.audioSource->Play();
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const FakeAudioOutput::Voice* voice =
            fake->voices.size() == 1 ? &fake->voices.begin()->second : nullptr;
        const bool voiceStarted = voice && voice->started && voice->loop &&
            voice->channelCount == 1 && voice->sampleRate == 44100 && voice->sampleCount == 6 &&
            voice->volume == 0.5f;

        // 볼륨 변경은 보이스를 다시 만들지 않고 따라간다.
        stage.audioSource->SetVolume(1.0f);
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool volumeFollowed = fake->createCalls == 1 && fake->voices.size() == 1 &&
            fake->voices.begin()->second.volume == 1.0f;

        // 일시정지는 커서를 남기는 정지다: 보이스는 살아 있고 멈춰 있을 뿐이다.
        static_cast<void>(stage.audioSource->Pause());
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool pausedKeepsVoice = fake->createCalls == 1 && fake->voices.size() == 1 &&
            !fake->voices.begin()->second.started;

        // 일시정지 중의 Play는 재개다: 커서를 버리지 않으므로 같은 보이스가 다시 시작된다.
        static_cast<void>(stage.audioSource->Play());
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool resumedSameVoice = fake->createCalls == 1 && fake->voices.size() == 1 &&
            fake->voices.begin()->second.started;

        // 재생 중의 Play 재호출은 재시작이다: 커서를 버리고 보이스를 새로 만든다.
        static_cast<void>(stage.audioSource->Play());
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool restartedFromStart = fake->createCalls == 2 &&
            fake->voices.size() == 1 && fake->voices.begin()->second.started;

        // 객체 비활성은 소리를 끊는다. 되살아나면 처음부터 다시 재생된다.
        stage.gameObject->SetActive(false);
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool deactivatedSilences = fake->voices.empty();
        stage.gameObject->SetActive(true);
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool reactivatedRestarts = fake->voices.size() == 1 && fake->createCalls == 3;

        // 반복 없는 재생이 끝에 닿으면 소스는 Stopped가 된다.
        stage.audioSource->SetLooping(false);
        static_cast<void>(stage.audioSource->Play());
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        for (const auto& [voiceId, fakeVoice] : fake->voices)
        {
            fake->finishedVoices.insert(voiceId);
        }
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool finishedStops = fake->voices.empty() &&
            stage.audioSource->GetPlaybackState() == AudioSource::PlaybackState::Stopped;

        // 장면이 내려가면 다음 동기화의 청소가 남은 보이스를 거둔다.
        static_cast<void>(stage.audioSource->Play());
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool playingBeforeUnload = fake->voices.size() == 1;
        const bool unloaded = stage.sceneManager.UnloadScene(stage.sceneId);
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool unloadSilences = fake->voices.empty();

        return Expect(played, "the source should accept Play") &&
            Expect(voiceStarted, "playing should create and start a matching voice") &&
            Expect(volumeFollowed, "a volume change should follow without recreating the voice") &&
            Expect(pausedKeepsVoice, "pausing should stop the voice but keep it") &&
            Expect(resumedSameVoice, "Play while paused should resume the same voice") &&
            Expect(restartedFromStart, "Play while playing should rebuild the voice") &&
            Expect(deactivatedSilences, "deactivating the object should destroy the voice") &&
            Expect(reactivatedRestarts, "reactivating should restart the still-playing source") &&
            Expect(finishedStops, "a finished non-loop voice should stop its source") &&
            Expect(playingBeforeUnload && unloaded, "the unload setup should hold") &&
            Expect(unloadSilences, "unloading the scene should sweep the voice away");
    }

    bool RunMissingOutputAndClipTests()
    {
        using GameEngine::Runtime::AudioSource;
        using GameEngine::Runtime::AudioSystem;

        // 출력이 아예 없는 머신: 소리 없이 상태만 남고, 아무것도 무너지지 않는다.
        AudioStage stage;
        if (!Expect(stage.Prepare(), "the audio stage should assemble"))
        {
            return false;
        }
        AudioSystem silent(nullptr);
        static_cast<void>(stage.audioSource->Play());
        silent.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool silentKeepsState =
            stage.audioSource->GetPlaybackState() == AudioSource::PlaybackState::Playing;

        // 디코딩되지 않는 클립: 보이스는 만들어지지 않고, 같은 재생의 실패는 한 번만 겪는다.
        auto fakeOwned = std::make_unique<FakeAudioOutput>();
        FakeAudioOutput* const fake = fakeOwned.get();
        AudioSystem system(std::move(fakeOwned));
        stage.audioSource->SetClip(GameEngine::Assets::AssetReference("missing.mp3"));
        static_cast<void>(stage.audioSource->Play());
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool unsupportedMakesNoVoice = fake->createCalls == 0 && fake->initializeCalls == 0;

        return Expect(silentKeepsState, "a missing output should leave playback state alone") &&
            Expect(unsupportedMakesNoVoice, "an unplayable clip should not create a voice");
    }

    /// <summary>
    /// 재생 중인 클립은 상주 스윕을 살아남는다.
    ///
    /// 오디오 시스템은 보이스마다 클립 페이로드의 shared_ptr을 쥐고, 출력은 그 샘플 메모리에서
    /// 직접 재생한다 — in-flight 프레임이나 백엔드의 resolve된 리소스와 같은 종류의 소유자다.
    /// 스윕이 메시와 이미지만 세면 그 파일은 아무도 안 쓰는 것처럼 보여 퇴거되고, 다시 요청하면
    /// 같은 PCM이 한 벌 더 상주한다.
    /// </summary>
    bool RunPlayingClipSurvivesSweepTests()
    {
        AudioStage stage;
        if (!Expect(stage.Prepare(), "the audio stage should assemble"))
        {
            return false;
        }
        auto output = std::make_unique<FakeAudioOutput>();
        FakeAudioOutput& fake = *output;
        GameEngine::Runtime::AudioSystem system(std::move(output));

        stage.audioSource->Play();
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool loadedForPlayback =
            fake.createCalls == 1 && stage.assetDatabase.GetLoadedPayloadCount() == 1;

        // 아무것도 참조하지 않는다고 말해도, 재생이 쥐고 있는 페이로드는 살아남아야 한다.
        stage.assetDatabase.UnloadUnreferenced({});
        const bool survivesWhilePlaying = stage.assetDatabase.GetLoadedPayloadCount() == 1;

        // 재생이 끝나 보이스가 거둬지면 다음 스윕이 수거한다.
        stage.audioSource->Stop();
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        stage.assetDatabase.UnloadUnreferenced({});
        const bool collectedAfterwards = stage.assetDatabase.GetLoadedPayloadCount() == 0;

        return Expect(loadedForPlayback, "playing a source should make its clip resident") &&
            Expect(
                survivesWhilePlaying,
                "a clip a voice is still playing should survive the residency sweep") &&
            Expect(
                collectedAfterwards, "a clip nothing plays any more should be swept away");
    }

    /// <summary>
    /// 재생 중에 출력 장치가 사라지는 경우다. 그런 보이스는 진행하지 않으므로, 끝났다고 말해
    /// 주지 않으면 반복 없는 소스가 로그 한 줄 없이 영원히 Playing으로 남는다.
    /// 사라진 뒤에는 장치 없는 머신과 같은 자리다: 상태만 있고 소리는 없으며, 다시 열지 않는다.
    /// </summary>
    bool RunLostOutputDeviceTests()
    {
        using GameEngine::Runtime::AudioSource;
        using GameEngine::Runtime::AudioSystem;

        AudioStage stage;
        if (!Expect(stage.Prepare(), "the audio stage should assemble"))
        {
            return false;
        }
        auto output = std::make_unique<FakeAudioOutput>();
        FakeAudioOutput& fake = *output;
        AudioSystem system(std::move(output));

        stage.audioSource->Play();
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool playing = fake.createCalls == 1 &&
            stage.audioSource->GetPlaybackState() == AudioSource::PlaybackState::Playing;

        // 장치가 사라진다.
        fake.failed = true;
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool releasedTheStuckVoice = fake.voices.empty() &&
            stage.audioSource->GetPlaybackState() == AudioSource::PlaybackState::Stopped;

        // 그 뒤의 재생은 조용하다. 출력을 다시 열지도, 보이스를 만들지도 않는다.
        stage.audioSource->Play();
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool staysSilent = fake.createCalls == 1 && fake.initializeCalls == 1 &&
            stage.audioSource->GetPlaybackState() == AudioSource::PlaybackState::Playing;

        return Expect(playing, "a prepared source should get a voice") &&
            Expect(
                releasedTheStuckVoice,
                "a lost output device should release the stuck voice and stop the source") &&
            Expect(
                staysSilent,
                "playback after a lost output device should stay silent without reopening it");
    }

    /// <summary>리스너가 없는 장면은 재생 상태를 유지하며 무음이다.</summary>
    bool RunNoListenerAttenuationTests()
    {
        using GameEngine::Runtime::AudioSystem;

        AudioStage stage;
        if (!Expect(stage.Prepare(), "the audio stage should assemble"))
        {
            return false;
        }
        stage.audioSource->SetSpatialBlend(1.0f);
        stage.audioSource->SetVolume(0.7f);

        auto fakeOwned = std::make_unique<FakeAudioOutput>();
        FakeAudioOutput* const fake = fakeOwned.get();
        AudioSystem system(std::move(fakeOwned));

        stage.audioSource->Play();
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool silentWithoutListener = fake->voices.size() == 1 &&
            fake->voices.begin()->second.volume == 0.0f;

        return Expect(
            silentWithoutListener,
            "with no listener in the scene, playback should continue silently");
    }

    /// <summary>minDistance 안쪽은 감쇠 없음, maxDistance 밖은 무음, 그 사이는 정확한 선형
    /// 보간임을 확인한다.</summary>
    bool RunDistanceAttenuationBoundaryTests()
    {
        using GameEngine::Math::Vector3;
        using GameEngine::Runtime::AudioListener;
        using GameEngine::Runtime::AudioSystem;

        AudioStage stage;
        if (!Expect(stage.Prepare(), "the audio stage should assemble"))
        {
            return false;
        }
        stage.audioSource->SetSpatialBlend(1.0f);
        stage.audioSource->SetVolume(0.8f);
        stage.audioSource->SetMinDistance(2.0f);
        stage.audioSource->SetMaxDistance(10.0f);
        stage.gameObject->GetTransform().SetPosition(Vector3(0.0f, 0.0f, 0.0f));

        AudioListener* const listener = stage.AddListener(Vector3(1.0f, 0.0f, 0.0f));
        if (!Expect(listener != nullptr, "the listener should attach"))
        {
            return false;
        }

        auto fakeOwned = std::make_unique<FakeAudioOutput>();
        FakeAudioOutput* const fake = fakeOwned.get();
        AudioSystem system(std::move(fakeOwned));
        stage.audioSource->Play();

        // 거리 1 < minDistance 2: 감쇠 없음.
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool withinMinIsFull = fake->voices.size() == 1 &&
            fake->voices.begin()->second.volume == 0.8f;

        // 거리 20 > maxDistance 10: 무음.
        listener->GetGameObject()->GetTransform().SetPosition(Vector3(20.0f, 0.0f, 0.0f));
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool beyondMaxIsSilent = fake->voices.begin()->second.volume == 0.0f;

        // 거리 6은 [2, 10] 한가운데다: attenuation = 1 - (6-2)/(10-2) = 0.5, 볼륨 = 0.8*0.5.
        listener->GetGameObject()->GetTransform().SetPosition(Vector3(6.0f, 0.0f, 0.0f));
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool betweenIsExactlyInterpolated = fake->voices.begin()->second.volume == 0.4f;

        return Expect(
                withinMinIsFull,
                "a listener within minDistance should not attenuate the source") &&
            Expect(
                beyondMaxIsSilent,
                "a listener beyond maxDistance with full spatialBlend should silence the source") &&
            Expect(
                betweenIsExactlyInterpolated,
                "a distance strictly between min and max should linearly interpolate the volume");
    }

    /// <summary>spatialBlend가 0이면 거리는 아무 효과가 없고, 0.5면 감쇠 없음과 완전 감쇠의
    /// 정확히 절반을 섞는다.</summary>
    bool RunSpatialBlendMixTests()
    {
        using GameEngine::Math::Vector3;
        using GameEngine::Runtime::AudioListener;
        using GameEngine::Runtime::AudioSystem;

        AudioStage stage;
        if (!Expect(stage.Prepare(), "the audio stage should assemble"))
        {
            return false;
        }
        stage.audioSource->SetVolume(1.0f);
        stage.audioSource->SetMinDistance(2.0f);
        stage.audioSource->SetMaxDistance(10.0f);

        AudioListener* const listener = stage.AddListener(Vector3(1000.0f, 0.0f, 0.0f));
        if (!Expect(listener != nullptr, "the listener should attach"))
        {
            return false;
        }

        auto fakeOwned = std::make_unique<FakeAudioOutput>();
        FakeAudioOutput* const fake = fakeOwned.get();
        AudioSystem system(std::move(fakeOwned));

        // spatialBlend = 0: 리스너가 아주 멀어도 거리는 효과가 없다.
        stage.audioSource->SetSpatialBlend(0.0f);
        stage.audioSource->Play();
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool zeroBlendIgnoresDistance = fake->voices.size() == 1 &&
            fake->voices.begin()->second.volume == 1.0f;

        // spatialBlend = 0.5, 거리 6 -> attenuation 0.5: lerp(1, 0.5, 0.5) = 0.75.
        listener->GetGameObject()->GetTransform().SetPosition(Vector3(6.0f, 0.0f, 0.0f));
        stage.audioSource->SetSpatialBlend(0.5f);
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool halfBlendIsExactlyHalfway = fake->voices.begin()->second.volume == 0.75f;

        return Expect(
                zeroBlendIgnoresDistance,
                "spatialBlend of 0 should leave the volume unattenuated regardless of distance") &&
            Expect(
                halfBlendIsExactlyHalfway,
                "spatialBlend of 0.5 should blend halfway between unattenuated and attenuated volume");
    }

    /// <summary>활성 리스너가 둘이면 인스턴스 id가 더 작은 쪽이 이긴다.</summary>
    bool RunMultipleListenersTests()
    {
        using GameEngine::Math::Vector3;
        using GameEngine::Runtime::AudioListener;
        using GameEngine::Runtime::AudioSystem;

        AudioStage stage;
        if (!Expect(stage.Prepare(), "the audio stage should assemble"))
        {
            return false;
        }
        stage.audioSource->SetVolume(1.0f);
        stage.audioSource->SetSpatialBlend(1.0f);
        stage.audioSource->SetMinDistance(1.0f);
        stage.audioSource->SetMaxDistance(11.0f);
        // 소스는 원점(Transform 기본값)에 남는다.

        // 먼저 만든 리스너가 더 작은 인스턴스 id를 받는다: 감쇠가 뚜렷이 다르도록 멀리 둔다.
        AudioListener* const firstListener = stage.AddListener(Vector3(11.0f, 0.0f, 0.0f));
        AudioListener* const secondListener = stage.AddListener(Vector3(1.0f, 0.0f, 0.0f));
        if (!Expect(
                firstListener != nullptr && secondListener != nullptr,
                "both listeners should attach") ||
            !Expect(
                firstListener->GetInstanceId() < secondListener->GetInstanceId(),
                "creation order should assign the first listener the lower instance id"))
        {
            return false;
        }

        auto fakeOwned = std::make_unique<FakeAudioOutput>();
        FakeAudioOutput* const fake = fakeOwned.get();
        AudioSystem system(std::move(fakeOwned));

        stage.audioSource->Play();
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        // 먼(첫 번째, 더 작은 id) 리스너가 이기면 거리 11 >= maxDistance가 소스를 무음으로
        // 만든다. 가까운 두 번째 리스너가 잘못 이겼다면 볼륨은 1.0으로 남았을 것이다.
        const bool lowerInstanceIdWins = fake->voices.size() == 1 &&
            fake->voices.begin()->second.volume == 0.0f;

        return Expect(
            lowerInstanceIdWins,
            "with two active listeners, the one with the lower instance id should be used");
    }

    /// <summary>비활성화되었거나 계층에서 비활성인 리스너는 아예 없는 것처럼 무시된다.</summary>
    bool RunDisabledListenerIgnoredTests()
    {
        using GameEngine::Math::Vector3;
        using GameEngine::Runtime::AudioListener;
        using GameEngine::Runtime::AudioSystem;

        AudioStage stage;
        if (!Expect(stage.Prepare(), "the audio stage should assemble"))
        {
            return false;
        }
        stage.audioSource->SetVolume(0.6f);
        stage.audioSource->SetSpatialBlend(1.0f);

        // 소스가 무시된 리스너를 잘못 봤다면 거리가 이 볼륨을 0으로 떨어뜨렸을 것이다.
        AudioListener* const listener = stage.AddListener(Vector3(1000.0f, 0.0f, 0.0f));
        if (!Expect(listener != nullptr, "the listener should attach"))
        {
            return false;
        }
        listener->SetEnabled(false);

        auto fakeOwned = std::make_unique<FakeAudioOutput>();
        FakeAudioOutput* const fake = fakeOwned.get();
        AudioSystem system(std::move(fakeOwned));

        stage.audioSource->Play();
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool disabledListenerIgnored = fake->voices.size() == 1 &&
            fake->voices.begin()->second.volume == 0.0f;

        // enabled를 되돌리되 계층에서 비활성으로 만든다: IsActiveAndEnabled는 여전히 false다.
        listener->SetEnabled(true);
        listener->GetGameObject()->SetActive(false);
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const bool inactiveListenerIgnored = fake->voices.begin()->second.volume == 0.0f;

        return Expect(
                disabledListenerIgnored,
                "a disabled listener should be treated as if no listener existed") &&
            Expect(
                inactiveListenerIgnored,
                "a listener inactive in the hierarchy should be treated as if no listener existed");
    }

    /// <summary>소스나 리스너를 두 Synchronize 호출 사이에 옮기면 다음 호출에서 보이스 볼륨이
    /// 바뀐다 — 한 번 계산해 캐시하는 것이 아니라 매 동기화마다 다시 계산된다는 증거다.</summary>
    bool RunLiveDistanceRecomputationTests()
    {
        using GameEngine::Math::Vector3;
        using GameEngine::Runtime::AudioListener;
        using GameEngine::Runtime::AudioSystem;

        AudioStage stage;
        if (!Expect(stage.Prepare(), "the audio stage should assemble"))
        {
            return false;
        }
        stage.audioSource->SetVolume(1.0f);
        stage.audioSource->SetSpatialBlend(1.0f);
        stage.audioSource->SetMinDistance(1.0f);
        stage.audioSource->SetMaxDistance(11.0f);

        AudioListener* const listener = stage.AddListener(Vector3(1.0f, 0.0f, 0.0f));
        if (!Expect(listener != nullptr, "the listener should attach"))
        {
            return false;
        }

        auto fakeOwned = std::make_unique<FakeAudioOutput>();
        FakeAudioOutput* const fake = fakeOwned.get();
        AudioSystem system(std::move(fakeOwned));

        stage.audioSource->Play();
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const float initialVolume =
            fake->voices.size() == 1 ? fake->voices.begin()->second.volume : -1.0f;

        // 리스너를 멀리 옮긴다: 다음 동기화에서 볼륨이 바뀐다.
        listener->GetGameObject()->GetTransform().SetPosition(Vector3(11.0f, 0.0f, 0.0f));
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const float afterListenerMoved = fake->voices.begin()->second.volume;

        // 소스를 리스너 쪽으로 되돌린다: 다시 동기화하면 볼륨도 되돌아온다.
        stage.gameObject->GetTransform().SetPosition(Vector3(11.0f, 0.0f, 0.0f));
        system.Synchronize(stage.sceneManager, stage.assetDatabase);
        const float afterSourceMoved = fake->voices.begin()->second.volume;

        return Expect(fake->voices.size() == 1, "a single voice should exist throughout") &&
            Expect(
                initialVolume == 1.0f,
                "the source should start at full volume next to the listener") &&
            Expect(
                afterListenerMoved == 0.0f,
                "moving the listener away should silence the source on the next synchronize") &&
            Expect(
                afterSourceMoved == 1.0f,
                "moving the source back to the listener should restore full volume on the next "
                "synchronize");
    }
}

bool RunWavAudioTests()
{
    return RunWavDecodeTests() && RunWavRejectionTests();
}

bool RunAudioSystemTests()
{
    return RunPlaybackLifetimeTests() && RunMissingOutputAndClipTests() &&
        RunPlayingClipSurvivesSweepTests() && RunLostOutputDeviceTests();
}

bool RunAudioListenerTests()
{
    return RunNoListenerAttenuationTests() && RunDistanceAttenuationBoundaryTests() &&
        RunSpatialBlendMixTests() && RunMultipleListenersTests() &&
        RunDisabledListenerIgnoredTests() && RunLiveDistanceRecomputationTests();
}

static const TestSupport::Registration gWavAudioTests{
    "Audio", "wav audio tests should pass", RunWavAudioTests };

static const TestSupport::Registration gAudioSystemTests{
    "Audio", "audio system tests should pass", RunAudioSystemTests };

static const TestSupport::Registration gAudioListenerTests{
    "Audio", "audio listener tests should pass", RunAudioListenerTests };
