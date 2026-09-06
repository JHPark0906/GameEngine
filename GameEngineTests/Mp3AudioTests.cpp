#include "Mp3AudioTests.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Assets/AssetDatabase.h"
#include "Assets/AssetImporter.h"
#include "Assets/AssetImporterRegistry.h"
#include "Assets/AssetReference.h"
#include "Assets/AudioData.h"
#include "Build/ContentPackWriter.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioDecoder.h"
#include "Platform/IAudioOutput.h"
#include "Platform/PackedContentSource.h"
#include "Platform/PlatformServices.h"
#include "Runtime/AudioSource.h"
#include "Runtime/AudioSystem.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using TestSupport::Expect;

    std::span<const std::byte> AsBytes(const std::string& contents)
    {
        return std::as_bytes(std::span(contents.data(), contents.size()));
    }

    bool SamePcm(const Platform::DecodedAudio& left, const Platform::DecodedAudio& right)
    {
        return left.channelCount == right.channelCount && left.sampleRate == right.sampleRate &&
               left.samples == right.samples;
    }

    bool SamePcm(const Assets::AudioData& left, const Platform::DecodedAudio& right)
    {
        return left.IsValid() && left.channelCount == right.channelCount &&
               left.sampleRate == right.sampleRate && left.samples == right.samples;
    }

    bool CheckDecoder(const std::string& fixture, Platform::DecodedAudio& decoded)
    {
        auto decoder = Platform::PlatformServices::CreateAudioDecoder();
        if (!Expect(decoder != nullptr, "this platform must provide an MP3 decoder"))
        {
            return false;
        }
        std::string error = "old error";
        if (!Expect(decoder->Decode(AsBytes(fixture), {}, decoded, error) && decoded.IsValid(),
                "the complete MP3 fixture must decode to valid PCM"))
        {
            return false;
        }

        // The generated fixture is a two-second mono tone at 44.1 kHz.
        // Allow encoder-delay/padding treatment to vary between decoders.
        const double duration =
            static_cast<double>(decoded.samples.size()) / decoded.channelCount / decoded.sampleRate;
        std::cout << "  MP3 PCM: " << decoded.samples.size() / decoded.channelCount << " frames, "
            << decoded.sampleRate << " Hz, " << duration << " seconds\n";
        bool passed =
            Expect(error.empty() && decoded.channelCount == 1 && decoded.sampleRate == 44100 &&
                       std::isfinite(duration) && duration > 1.8 && duration < 2.1 &&
                       std::ranges::any_of(
                           decoded.samples, [](const std::int16_t sample) { return sample != 0; }),
                "MP3 decoding must preserve sample rate, channel count, non-silent PCM and duration");

        const Platform::DecodedAudio expected = decoded;
        const auto reject = [&](const std::span<const std::byte> bytes,
                                const Platform::AudioDecodeLimits limits, const char* message)
        {
            error.clear();
            return Expect(!decoder->Decode(bytes, limits, decoded, error) && !error.empty() &&
                              SamePcm(decoded, expected),
                message);
        };
        passed &= reject({}, {}, "empty MP3 input must fail without replacing existing PCM");
        const std::string corrupt(256, '\x55');
        passed &= reject(AsBytes(corrupt), {},
            "corrupt MP3 input must fail with a reason and leave existing PCM unchanged");
        passed &= reject(AsBytes(fixture), { 16 },
            "a tiny PCM budget must reject decoding without exposing a partial output");
        passed &= reject(AsBytes(fixture), { 0 },
            "a zero PCM budget must reject decoding without replacing existing PCM");

        Platform::DecodedAudio recovered;
        error = "old error";
        passed &= Expect(decoder->Decode(AsBytes(fixture), {}, recovered, error) && error.empty() &&
                             SamePcm(recovered, expected),
            "the same decoder must recover from malformed input and budget failures");
        return passed;
    }

    bool CheckImporter(const std::string& fixture, const Platform::DecodedAudio& decoded)
    {
        const Assets::IAssetImporter* const importer =
            Assets::AssetImporterRegistry::Find("Audio/synthetic-tone.MP3");
        if (!Expect(Assets::AssetImporterRegistry::IsSupportedExtension(".mp3") && importer &&
                        importer->GetAssetType() == Assets::AssetType::AudioClip,
                "MP3 must be registered as a case-insensitive built-in audio importer"))
        {
            return false;
        }

        const Assets::ImportIdentity identity{ 0x1234, 0x5678 };
        Assets::ImportedContents structure;
        std::string error;
        bool passed = Expect(importer->Import("Audio/synthetic-tone.mp3", {}, identity,
                                 Assets::ImportMode::Structure, structure, error) &&
                                 structure.subAssets.size() == 1 &&
                                 structure.subAssets.front().type == Assets::AssetType::AudioClip &&
                                 structure.audioClips.empty(),
            "MP3 discovery must expose one audio asset without decoding a payload");

        Assets::ImportedContents full;
        if (!Expect(importer->Import("Audio/synthetic-tone.mp3", AsBytes(fixture), identity,
                        Assets::ImportMode::Full, full, error) &&
                        full.subAssets.size() == 1 && full.audioClips.size() == 1 &&
                        full.audioClips.front(),
                "full MP3 import must create one PCM audio payload"))
        {
            return false;
        }
        passed &= Expect(SamePcm(*full.audioClips.front(), decoded),
            "the MP3 importer must publish the decoder's complete PCM samples");

        Assets::ImportedContents empty;
        error.clear();
        passed &= Expect(importer->Import("empty.mp3", {}, identity, Assets::ImportMode::Full,
                             empty, error) && empty.subAssets.size() == 1 &&
                             empty.audioClips.size() == 1 && !empty.audioClips.front(),
            "an empty MP3 must remain listed while exposing no playable audio payload");
        Assets::ImportedContents corrupt;
        error.clear();
        const std::string invalidBytes("This is not an MPEG audio stream.");
        passed &= Expect(importer->Import("corrupt.mp3", AsBytes(invalidBytes), identity,
                             Assets::ImportMode::Full, corrupt, error) &&
                             corrupt.subAssets.size() == 1 && corrupt.audioClips.size() == 1 &&
                             !corrupt.audioClips.front(),
            "a malformed MP3 must remain listed while exposing no playable audio payload");
        return passed;
    }

    bool CheckSilentPlayback(Assets::AssetDatabase& assets, const Assets::AudioData& decoded)
    {
        Runtime::ObjectRegistry objects;
        Runtime::Input input;
        Runtime::RuntimeContext context{ objects, input };
        Runtime::SceneManager scenes{ context };
        auto scene = std::make_unique<Runtime::Scene>(context, "Packed MP3 playback");
        auto* const source = scene->CreateGameObject("Source")->AddComponent<Runtime::AudioSource>();
        source->SetClip(Assets::AssetReference("Audio/synthetic-tone.mp3"));
        if (!Expect(scenes.AddScene(std::move(scene)) != 0,
                "the MP3 playback scene must be available to the audio system"))
        {
            return false;
        }
        Runtime::AudioSystem system(nullptr);
        bool passed = Expect(source->GetClipDurationSeconds() == 0.0,
            "an unplayed MP3 source must begin with unknown duration");
        source->SetLooping(true);
        source->SetPitch(2.0f);
        passed &= Expect(source->Play(), "an MP3 audio source must accept Play without a device");
        system.Synchronize(scenes, assets);
        passed &=
            Expect(std::abs(source->GetClipDurationSeconds() - decoded.GetDurationSeconds()) < 1e-12,
                "silent MP3 playback must expose decoded duration independent of pitch and looping");
        source->Stop();
        system.Synchronize(scenes, assets);
        passed &= Expect(source->GetClipDurationSeconds() == decoded.GetDurationSeconds(),
            "stopping silent MP3 playback must preserve the known clip duration");
        source->SetClip(Assets::AssetReference("Audio/corrupt.mp3"));
        passed &= Expect(source->GetClipDurationSeconds() == 0.0 && source->Play(),
            "an invalid MP3 replacement must invalidate the previous duration");
        system.Synchronize(scenes, assets);
        passed &= Expect(source->GetClipDurationSeconds() == 0.0,
            "failed MP3 decoding must not retain the duration of a previous clip");
        return passed;
    }

    bool CheckContentSources(const std::string& fixture, const Platform::DecodedAudio& decoded)
    {
        TestSupport::TemporaryDirectory temporary("mp3-content");
        const auto root = temporary.GetPath();
        const auto contentPath = root / "Content";
        const auto packedPath = root / "Packed.bin";
        if (!Expect(
                TestSupport::WriteFile(contentPath / "Audio.gameproject", "{}") &&
                    TestSupport::WriteFile(contentPath / "Audio/synthetic-tone.mp3", fixture) &&
                    TestSupport::WriteFile(contentPath / "Audio/corrupt.mp3", "invalid MPEG audio") &&
                    TestSupport::WriteFile(contentPath / "Audio/empty.mp3", "") &&
                    TestSupport::WriteFile(packedPath, "MZ test executable"),
                "MP3 directory and pack fixtures must be writable"))
        {
            return false;
        }

        Platform::DirectoryContentSource directory(contentPath);
        Assets::AssetDatabase directoryAssets;
        if (!Expect(directoryAssets.Refresh(directory) && directoryAssets.GetLoadedPayloadCount() == 0,
                "refreshing directory MP3 assets must not eagerly decode PCM"))
        {
            return false;
        }
        const auto directoryClip =
            directoryAssets.LoadAudioClip(Assets::AssetReference("Audio/synthetic-tone.mp3"));
        if (!Expect(directoryClip && SamePcm(*directoryClip, decoded),
                "directory content must load the complete MP3 PCM payload"))
        {
            return false;
        }
        bool passed = Expect(
            directoryAssets.LoadAudioClip(Assets::AssetReference("Audio/synthetic-tone.mp3")) == directoryClip,
            "repeated MP3 loads must share one resident PCM payload");

        const std::vector<std::filesystem::path> packedPaths{ "Audio.gameproject", "Audio/synthetic-tone.mp3",
            "Audio/corrupt.mp3", "Audio/empty.mp3" };
        if (!Expect(Build::AppendContentPack(packedPath, contentPath, packedPaths),
                "an MP3 project must pack alongside the executable bytes"))
        {
            return false;
        }
        Platform::PackedContentSource packed(packedPath);
        Assets::AssetDatabase packedAssets;
        if (!Expect(packed.IsValid() && packed.ResolveFilePath("Audio/synthetic-tone.mp3").empty() &&
                        packedAssets.Refresh(packed) && packedAssets.GetLoadedPayloadCount() == 0,
                "packed MP3 discovery must work without an external audio path or PCM load"))
        {
            return false;
        }
        const auto packedClip = packedAssets.LoadAudioClip(Assets::AssetReference("Audio/synthetic-tone.mp3"));
        if (!Expect(packedClip && SamePcm(*packedClip, decoded),
                "an MP3 read from packed bytes must match directory PCM exactly"))
        {
            return false;
        }
        passed &= Expect(!packedAssets.LoadAudioClip(Assets::AssetReference("Audio/empty.mp3")) &&
                             !packedAssets.LoadAudioClip(Assets::AssetReference("Audio/corrupt.mp3")),
            "packed empty and corrupt MP3 assets must never produce playable PCM");
        passed &= CheckSilentPlayback(packedAssets, *packedClip);
        return passed;
    }
}

bool RunMp3AudioTests()
{
    const auto fixturePath =
        std::filesystem::path(__FILE__).parent_path() / "Content/Audio/synthetic-tone.mp3";
    const std::string fixture = TestSupport::ReadFile(fixturePath);
    if (!Expect(!fixture.empty(),
            "the generated MP3 tone fixture must be present and readable"))
    {
        return false;
    }
    GameEngine::Platform::DecodedAudio decoded;
    bool passed = CheckDecoder(fixture, decoded);
    if (!decoded.IsValid())
    {
        return false;
    }
    passed &= CheckImporter(fixture, decoded);
    passed &= CheckContentSources(fixture, decoded);
    return passed;
}

static const TestSupport::Registration gMp3AudioTests{ "Audio",
    "MP3 decoding and packed audio playback tests should pass", RunMp3AudioTests };
