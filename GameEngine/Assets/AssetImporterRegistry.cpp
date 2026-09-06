#include "pch.h"
#include "AssetImporterRegistry.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AssetReference.h"
#include "FbxImporter.h"
#include "ImageLimits.h"
#include "WavAudio.h"
#include "../Animation/AnimationClip.h"
#include "../Animation/Skeleton.h"
#include "../Core/Json.h"
#include "../Math/Color.h"
#include "../Platform/PlatformServices.h"
#include "../Platform/IAudioDecoder.h"
#include "../Diagnostics/Debug.h"

namespace GameEngine::Assets
{

namespace
{
    std::string PathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string value = path.generic_u8string();
        return {
            reinterpret_cast<const char*>(value.data()),
            reinterpret_cast<const char*>(value.data() + value.size())
        };
    }

    /// <summary>
    /// 파일 자체가 곧 에셋인 경우다. 대부분의 포맷이 이렇다: 장면, 이미지, 폰트, 소리. 확장자가
    /// 이미 두 질문 모두에 답하므로 임포터는 파일을 열지 않는다.
    /// </summary>
    class SingleAssetImporter final : public IAssetImporter
    {
    public:
        explicit SingleAssetImporter(const AssetType type)
            : mType(type)
        {
        }

        [[nodiscard]] AssetType GetAssetType() const override { return mType; }

        [[nodiscard]] bool Import(
            const std::filesystem::path& relativePath,
            std::span<const std::byte>,
            const ImportIdentity&,
            const ImportMode,
            ImportedContents& contents,
            std::string&) const override
        {
            // The extension already answers both questions, so neither mode opens the file.
            contents.subAssets.push_back({ mType, PathToUtf8(relativePath.stem()) });
            return true;
        }

    private:
        AssetType mType;
    };

    /// <summary>
    /// 이미지 파일이다. 임포트 시점에 디코딩해서 나중에는 아무것도 디코딩하지 않는다. 디코더는
    /// 한 번 만들어 재사용한다: 이미지마다 만들면 프로젝트가 담은 파일 수만큼 플랫폼 이미징
    /// 스택을 여는 셈이다.
    /// </summary>
    class SpriteImporter final : public IAssetImporter
    {
    public:
        [[nodiscard]] AssetType GetAssetType() const override { return AssetType::Sprite; }

        [[nodiscard]] bool Import(
            const std::filesystem::path& relativePath,
            const std::span<const std::byte> fileBytes,
            const ImportIdentity& identity,
            const ImportMode mode,
            ImportedContents& contents,
            std::string& error) const override
        {
            // An image file holds one sprite, named after the file. Nothing has to be read to know
            // that, which is the whole reason a scan is cheap: decoding is the expensive part and a
            // scan does not need it.
            contents.subAssets.push_back({ AssetType::Sprite, PathToUtf8(relativePath.stem()) });
            if (mode == ImportMode::Structure)
            {
                return true;
            }

            if (!mDecoder)
            {
                mDecoder = Platform::PlatformServices::CreateImageDecoder();
                if (!mDecoder || !mDecoder->Initialize())
                {
                    mDecoder.reset();
                    error = "the platform image decoder is unavailable";
                    return false;
                }
            }

            // A failed decode still leaves the sprite listed, without pixels — the file keeps
            // appearing in the editor as what it is, and the draw that needed them is skipped. A
            // model is the other case: if it will not parse, how many meshes it holds is unknown,
            // and inventing one would be a guess.
            Platform::DecodedImage decoded;
            std::shared_ptr<TextureData> texture;
            if (mDecoder->Decode(
                    fileBytes, PathToUtf8(relativePath), GetImageDecodeLimits(), decoded) &&
                decoded.IsValid())
            {
                // Decoded straight into the form a frame carries. The pixels move rather than copy,
                // so this is the only place they exist.
                texture = std::make_shared<TextureData>();
                texture->id = identity.MakeResourceId(Assets::ResourceIdDomain::Texture, 0);
                texture->width = decoded.width;
                texture->height = decoded.height;
                texture->pixels = std::move(decoded.pixels);
            }
            else
            {
                Diagnostics::Debug::LogError(
                    "An image asset holds no usable pixels. path=", relativePath.string());
            }

            contents.images.push_back(std::move(texture));
            return true;
        }

    private:
        mutable std::unique_ptr<Platform::IImageDecoder> mDecoder;
    };

    /// <summary>
    /// 모델 파일이다. 배치하는 geometry마다 메시 에셋 하나를 담는다. 이 인터페이스가 존재하는
    /// 이유가 되는 임포터다: 개수는 확장자가 아니라 파일의 속성이다.
    ///
    /// 스킨(Skin)을 가진 파일은 먼저 그쪽으로 읽는다: 뼈에 매인 형상을 정적 메시로도 함께
    /// 내놓으면 포즈를 지을 수 없는 죽은 사본이 목록에 남아 고르는 사람을 헷갈리게 한다. 그래서
    /// Skin 파일은 스킨드 메시·골격·클립만 낸다. 스킨 없는 rigid animation은 기존 정적 메시
    /// localId를 유지하고 그 뒤에 스킨드 메시·골격·클립을 추가한다.
    /// </summary>
    class MeshImporter final : public IAssetImporter
    {
    public:
        [[nodiscard]] AssetType GetAssetType() const override { return AssetType::Mesh; }

        [[nodiscard]] bool Import(
            const std::filesystem::path&,
            const std::span<const std::byte> fileBytes,
            const ImportIdentity& identity,
            const ImportMode mode,
            ImportedContents& contents,
            std::string& error) const override
        {
            // A model is the one format whose structure is a property of the file, so even a scan
            // has to parse it. What a scan skips is the conversion below, which is where the
            // vertices are actually allocated.
            Animation::Skeleton skeleton;
            std::vector<SkinnedMeshData> skinnedMeshes;
            std::vector<std::string> skinnedMeshNames;
            std::vector<Animation::AnimationClip> clips;
            std::string skeletonError;
            bool rigidAnimation = false;
            const bool hasSkeleton = FbxImporter::LoadSkeleton(
                fileBytes, skinnedMeshes, skinnedMeshNames, skeleton, clips, skeletonError, &rigidAnimation);

            std::vector<ImportedMesh> meshes;
            if ((!hasSkeleton || rigidAnimation) && !FbxImporter::Load(fileBytes, meshes, error))
            {
                return false;
            }

            const std::size_t totalCount =
                meshes.size() + skinnedMeshes.size() + (hasSkeleton ? 1 : 0) + clips.size();
            contents.subAssets.reserve(totalCount);
            for (std::size_t index = 0; index < meshes.size(); ++index)
            {
                std::string name = meshes[index].name;
                if (name.empty())
                {
                    // Every asset needs something to be listed by. Falling back to the position
                    // keeps the name unique within the file, which a modelling tool does not promise.
                    name = "Mesh " + std::to_string(index);
                }
                contents.subAssets.push_back({ AssetType::Mesh, std::move(name) });
            }
            for (std::size_t index = 0; index < skinnedMeshes.size(); ++index)
            {
                std::string name = skinnedMeshNames[index];
                if (name.empty())
                {
                    name = "Skinned Mesh " + std::to_string(index);
                }
                contents.subAssets.push_back({ AssetType::SkinnedMesh, std::move(name) });
            }
            if (hasSkeleton)
            {
                contents.subAssets.push_back({ AssetType::Skeleton, "Skeleton" });
            }
            for (std::size_t index = 0; index < clips.size(); ++index)
            {
                std::string name = clips[index].name;
                if (name.empty())
                {
                    name = "Clip " + std::to_string(index);
                }
                contents.subAssets.push_back({ AssetType::AnimationClip, std::move(name) });
            }
            if (mode == ImportMode::Structure)
            {
                return true;
            }

            // Each vector is padded to the full sub-asset count so a reference's local id, which
            // spans every kind together, still indexes it directly -- AssetDatabase::LoadMesh and
            // its siblings all rely on that.
            contents.meshes.resize(totalCount);
            contents.skinnedMeshes.resize(totalCount);
            contents.skeletons.resize(totalCount);
            contents.animationClips.resize(totalCount);

            std::size_t position = 0;
            for (std::size_t index = 0; index < meshes.size(); ++index, ++position)
            {
                // Converted into the engine's vertex layout here, once, and kept in that form. The
                // parsed shape is a step on the way and does not outlive this function.
                const ImportedMesh& imported = meshes[index];
                auto mesh = std::make_shared<MeshData>();
                mesh->id = identity.MakeResourceId(
                    Assets::ResourceIdDomain::Mesh, static_cast<std::uint32_t>(index));
                mesh->vertices.reserve(imported.vertices.size());
                for (const ImportedMeshVertex& vertex : imported.vertices)
                {
                    mesh->vertices.push_back({
                        { vertex.position[0], vertex.position[1], vertex.position[2] },
                        { vertex.normal[0], vertex.normal[1], vertex.normal[2] },
                        { vertex.textureCoordinate[0], vertex.textureCoordinate[1] }
                    });
                }
                mesh->indices.assign(imported.indices.begin(), imported.indices.end());
                // 정점을 여기서 마지막으로 만지므로 상자도 여기서 얻는다. 이 뒤로는 그리는
                // 쪽도 컬링도 정점을 다시 훑지 않는다.
                mesh->bounds = ComputeBounds(mesh->vertices);
                if (!mesh->IsValid())
                {
                    Diagnostics::Debug::LogError(
                        "An imported mesh holds no drawable geometry. index=", index);
                    mesh.reset();
                }
                contents.meshes[position] = std::move(mesh);
            }
            for (std::size_t index = 0; index < skinnedMeshes.size(); ++index, ++position)
            {
                auto mesh = std::make_shared<SkinnedMeshData>(std::move(skinnedMeshes[index]));
                mesh->id = identity.MakeResourceId(
                    Assets::ResourceIdDomain::SkinnedMesh, static_cast<std::uint32_t>(index));
                if (!mesh->IsValid())
                {
                    Diagnostics::Debug::LogError(
                        "An imported skinned mesh holds no drawable geometry. index=", index);
                    mesh.reset();
                }
                contents.skinnedMeshes[position] = std::move(mesh);
            }
            if (hasSkeleton)
            {
                contents.skeletons[position] = std::make_shared<Animation::Skeleton>(std::move(skeleton));
                ++position;
            }
            for (std::size_t index = 0; index < clips.size(); ++index, ++position)
            {
                contents.animationClips[position] =
                    std::make_shared<Animation::AnimationClip>(std::move(clips[index]));
            }
            return true;
        }
    };

    /// <summary>
    /// WAV 파일이다. 임포트 시점에 디코딩해 재생 시점에는 다시 파싱하지 않는다.
    /// 지원하지 않는 WAV 형식이면 클립은 페이로드 없이 목록에 남고 재생 시스템이 로드 실패를 알린다.
    /// </summary>
    class WavAudioImporter final : public IAssetImporter
    {
    public:
        [[nodiscard]] AssetType GetAssetType() const override { return AssetType::AudioClip; }

        [[nodiscard]] bool Import(
            const std::filesystem::path& relativePath,
            const std::span<const std::byte> fileBytes,
            const ImportIdentity& identity,
            const ImportMode mode,
            ImportedContents& contents,
            std::string&) const override
        {
            // WAV 파일 하나는 클립 하나다. 스캔은 그 사실만 필요하므로 파일을 읽지 않는다.
            contents.subAssets.push_back({ AssetType::AudioClip, PathToUtf8(relativePath.stem()) });
            if (mode == ImportMode::Structure)
            {
                return true;
            }

            DecodedWavAudio decoded;
            std::string decodeError;
            std::shared_ptr<AudioData> audio;
            if (DecodeWavAudio(fileBytes, decoded, decodeError))
            {
                audio = std::make_shared<AudioData>();
                audio->id = identity.MakeResourceId(Assets::ResourceIdDomain::Audio, 0);
                audio->channelCount = decoded.channelCount;
                audio->sampleRate = decoded.sampleRate;
                audio->samples = std::move(decoded.samples);
            }
            else
            {
                Diagnostics::Debug::LogError(
                    "An audio asset cannot be decoded. path=", relativePath.string(),
                    ", error=", decodeError);
            }
            contents.audioClips.push_back(std::move(audio));
            return true;
        }
    };

    /// <summary>MP3는 플랫폼 디코더가 PCM으로 바꾸고, 그 뒤는 WAV와 같은 오디오 페이로드다.</summary>
    class Mp3AudioImporter final : public IAssetImporter
    {
    public:
        [[nodiscard]] AssetType GetAssetType() const override { return AssetType::AudioClip; }

        [[nodiscard]] bool Import(const std::filesystem::path& relativePath,
            const std::span<const std::byte> fileBytes, const ImportIdentity& identity,
            const ImportMode mode, ImportedContents& contents, std::string&) const override
        {
            contents.subAssets.push_back({ AssetType::AudioClip, PathToUtf8(relativePath.stem()) });
            if (mode == ImportMode::Structure) return true;

            const std::unique_ptr<Platform::IAudioDecoder> decoder =
                Platform::PlatformServices::CreateAudioDecoder();
            Platform::DecodedAudio decoded;
            std::string decodeError;
            std::shared_ptr<AudioData> audio;
            if (decoder && decoder->Decode(fileBytes, Platform::AudioDecodeLimits{}, decoded, decodeError))
            {
                audio = std::make_shared<AudioData>();
                audio->id = identity.MakeResourceId(Assets::ResourceIdDomain::Audio, 0);
                audio->channelCount = decoded.channelCount;
                audio->sampleRate = decoded.sampleRate;
                audio->samples = std::move(decoded.samples);
            }
            else
            {
                Diagnostics::Debug::LogError(
                    "An audio asset cannot be decoded. path=", relativePath.string(),
                    ", error=", decoder ? decodeError : "no platform audio decoder is available");
            }
            contents.audioClips.push_back(std::move(audio));
            return true;
        }
    };

    /// <summary>
    /// <c>.material</c> 파일이다. 사람이 만드는 JSON이라 소스가 곧 페이로드다 — 디코딩할
    /// 이미지도, 파싱할 모델도 없으니 스캔은 파일을 열지 않고, Full 임포트에서만 그 JSON을
    /// 읽는다.
    /// </summary>
    class MaterialImporter final : public IAssetImporter
    {
    public:
        [[nodiscard]] AssetType GetAssetType() const override { return AssetType::Material; }

        [[nodiscard]] bool Import(
            const std::filesystem::path& relativePath,
            const std::span<const std::byte> fileBytes,
            const ImportIdentity&,
            const ImportMode mode,
            ImportedContents& contents,
            std::string& error) const override
        {
            // 파일 하나는 머티리얼 하나다. 스캔은 그 사실만 필요하므로 파일을 읽지 않는다.
            contents.subAssets.push_back(
                { AssetType::Material, PathToUtf8(relativePath.stem()) });
            if (mode == ImportMode::Structure)
            {
                return true;
            }

            try
            {
                const Core::Json root = Core::Json::ParseBytes(fileBytes);
                auto material = std::make_shared<MaterialData>();
                material->texture = AssetReference::Parse(root.Value("texture", std::string{}));
                if (const Core::Json* const tint = root.Find("tint"))
                {
                    const std::vector<Core::Json>& channels = tint->AsArray();
                    if (channels.size() == 4)
                    {
                        material->tint = Math::Color(
                            channels[0].Get<float>(), channels[1].Get<float>(),
                            channels[2].Get<float>(), channels[3].Get<float>());
                    }
                    else
                    {
                        Diagnostics::Debug::LogWarning(
                            "A material's tint should have four channels; using white "
                            "instead. path=", relativePath.string());
                    }
                }
                contents.materials.push_back(std::move(material));
                return true;
            }
            catch (const std::exception& parseError)
            {
                error = parseError.what();
                return false;
            }
        }
    };

    struct ImporterEntry
    {
        std::string extension;
        const IAssetImporter* importer;
    };

    /// <summary>
    /// 임포트되는 모든 확장자이다. 처음 쓰일 때 엔진 자체 포맷들로 채워진다.
    ///
    /// 임포터들이 네임스페이스 범위 객체가 아니라 함수 지역 static인 것은, 이 번역 단위의
    /// 초기화가 다른 번역 단위와 어떤 순서로 실행되는지에 아무것도 의존하지 않게 하기 위해서다.
    /// </summary>
    [[nodiscard]] std::vector<ImporterEntry>& GetImporters()
    {
        static const SingleAssetImporter projectSettingsImporter{ AssetType::ProjectSettings };
        static const SingleAssetImporter sceneImporter{ AssetType::Scene };
        static const SpriteImporter spriteImporter;
        static const SingleAssetImporter fontImporter{ AssetType::Font };
        static const SingleAssetImporter audioClipImporter{ AssetType::AudioClip };
        static const SingleAssetImporter iconImporter{ AssetType::Icon };
        static const WavAudioImporter wavAudioImporter;
        static const Mp3AudioImporter mp3AudioImporter;
        static const MeshImporter meshImporter;
        static const MaterialImporter materialImporter;

        // Extensions are lowercase; a path's extension is lowered before it is looked up.
        static std::vector<ImporterEntry> importers{
            { ".gameproject", &projectSettingsImporter },
            { ".scene", &sceneImporter },
            { ".png", &spriteImporter },
            { ".jpg", &spriteImporter },
            { ".jpeg", &spriteImporter },
            { ".fbx", &meshImporter },
            { ".ttf", &fontImporter },
            { ".otf", &fontImporter },
            { ".ico", &iconImporter },
            // WAV와 MP3는 PCM으로 디코딩된다. OGG는 아직 페이로드 없이 목록에만 남는다.
            { ".wav", &wavAudioImporter },
            { ".mp3", &mp3AudioImporter },
            { ".ogg", &audioClipImporter },
            { ".material", &materialImporter },
        };
        return importers;
    }

    [[nodiscard]] const IAssetImporter* FindByExtension(const std::string_view extension)
    {
        const std::vector<ImporterEntry>& importers = GetImporters();
        const auto entry = std::ranges::find(importers, extension, &ImporterEntry::extension);
        return entry == importers.end() ? nullptr : entry->importer;
    }
}

const IAssetImporter* AssetImporterRegistry::Find(const std::filesystem::path& path)
{
    std::string extension = PathToUtf8(path.extension());
    std::ranges::transform(
        extension,
        extension.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return FindByExtension(extension);
}

bool AssetImporterRegistry::IsSupportedExtension(const std::string_view lowercaseExtension)
{
    return FindByExtension(lowercaseExtension) != nullptr;
}

bool AssetImporterRegistry::Register(
    const std::string_view lowercaseExtension, const IAssetImporter& importer)
{
    if (lowercaseExtension.size() < 2 || lowercaseExtension.front() != '.')
    {
        Diagnostics::Debug::LogError(
            "An asset importer extension must start with a dot. extension=", lowercaseExtension);
        return false;
    }
    if (FindByExtension(lowercaseExtension) != nullptr)
    {
        Diagnostics::Debug::LogError(
            "An asset importer is already registered for this extension. extension=",
            lowercaseExtension);
        return false;
    }

    GetImporters().push_back({ std::string(lowercaseExtension), &importer });
    return true;
}

AssetImporterRegistry::Registrations AssetImporterRegistry::Snapshot()
{
    Registrations registrations;
    const std::vector<ImporterEntry>& importers = GetImporters();
    registrations.reserve(importers.size());
    for (const ImporterEntry& entry : importers)
    {
        registrations.push_back({ entry.extension, entry.importer });
    }
    return registrations;
}

void AssetImporterRegistry::Restore(const Registrations& registrations)
{
    std::vector<ImporterEntry>& importers = GetImporters();
    importers.clear();
    importers.reserve(registrations.size());
    for (const auto& [extension, importer] : registrations)
    {
        importers.push_back({ extension, importer });
    }
}

bool AssetImporterRegistry::Unregister(const std::string_view lowercaseExtension)
{
    std::vector<ImporterEntry>& importers = GetImporters();
    return std::erase_if(
        importers,
        [lowercaseExtension](const ImporterEntry& entry)
        {
            return entry.extension == lowercaseExtension;
        }) != 0;
}

}
