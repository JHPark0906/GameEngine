#include "MeshFadeTests.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "Animation/AnimationClip.h"
#include "Animation/Skeleton.h"
#include "Assets/AssetImporter.h"
#include "Assets/AssetImporterRegistry.h"
#include "BackendPixelSupport.h"
#include "Core/ResourceId.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderColorPolicy.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Animator.h"
#include "Runtime/Camera.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Light.h"
#include "Runtime/MeshRenderer.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"
#include "SceneRendering/SceneRenderPass.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Serialization/SceneSerializer.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using TestSupport::Expect;

    class FadeQuadImporter final : public Assets::IAssetImporter
    {
    public:
        [[nodiscard]] Assets::AssetType GetAssetType() const override { return Assets::AssetType::Mesh; }
        [[nodiscard]] bool Import(const std::filesystem::path&, std::span<const std::byte>,
            const Assets::ImportIdentity& identity, const Assets::ImportMode mode,
            Assets::ImportedContents& contents, std::string&) const override
        {
            contents.subAssets = { { Assets::AssetType::Mesh, "Static" },
                { Assets::AssetType::SkinnedMesh, "Skinned" },
                { Assets::AssetType::Skeleton, "Skeleton" },
                { Assets::AssetType::AnimationClip, "Still pose" } };
            if (mode == Assets::ImportMode::Structure) return true;
            auto mesh = std::make_shared<Assets::MeshData>();
            mesh->id = identity.MakeResourceId(Core::ResourceIdDomain::Mesh, 0);
            for (const auto point : { Math::Vector2{ -.5f, -.5f }, Math::Vector2{ -.5f, .5f },
                     Math::Vector2{ .5f, .5f }, Math::Vector2{ .5f, -.5f } })
            {
                Core::MeshVertex vertex;
                vertex.position = { point.GetX(), point.GetY(), 2 };
                vertex.normal = { 0, 0, -1 };
                vertex.textureCoordinate = { 0, 0 };
                mesh->vertices.push_back(vertex);
            }
            mesh->indices = { 0, 1, 2, 0, 2, 3 };
            mesh->bounds = Assets::ComputeBounds(mesh->vertices);
            auto skinned = std::make_shared<Assets::SkinnedMeshData>();
            skinned->id = identity.MakeResourceId(Core::ResourceIdDomain::SkinnedMesh, 0);
            for (const auto& vertex : mesh->vertices)
            {
                Core::SkinnedMeshVertex output;
                output.position = vertex.position;
                output.normal = vertex.normal;
                output.textureCoordinate = vertex.textureCoordinate;
                output.boneIndices = { 0, 0, 0, 0 };
                output.boneWeights = { 1, 0, 0, 0 };
                skinned->vertices.push_back(output);
            }
            skinned->indices = mesh->indices;
            skinned->bounds = Assets::ComputeSkinnedBounds(skinned->vertices);
            auto skeleton = std::make_shared<Animation::Skeleton>();
            skeleton->bones.push_back(Animation::Bone{});
            auto clip = std::make_shared<Animation::AnimationClip>();
            clip->duration = 1;
            contents.meshes.resize(4);
            contents.skinnedMeshes.resize(4);
            contents.skeletons.resize(4);
            contents.animationClips.resize(4);
            contents.meshes[0] = std::move(mesh);
            contents.skinnedMeshes[1] = std::move(skinned);
            contents.skeletons[2] = std::move(skeleton);
            contents.animationClips[3] = std::move(clip);
            return true;
        }
    };

    bool PixelNear(const TestSupport::Rgba actual, const TestSupport::Rgba expected)
    {
        return std::abs(int(actual.r) - int(expected.r)) <= 2 &&
            std::abs(int(actual.g) - int(expected.g)) <= 2 &&
            std::abs(int(actual.b) - int(expected.b)) <= 2 &&
            std::abs(int(actual.a) - int(expected.a)) <= 2;
    }
}

bool RunMeshFadeTests()
{
    using namespace GameEngine;
    const TestSupport::RegistryScope registries;
    static const FadeQuadImporter importer;
    if (!Expect(Assets::AssetImporterRegistry::Register(".fadequad", importer),
            "the fading mesh fixture importer must register")) return false;
    const TestSupport::TemporaryDirectory directory("mesh-fade");
    const auto root = directory.GetPath();
    if (!Expect(TestSupport::WriteFile(root / "Fade.gameproject", "{}") &&
            TestSupport::WriteFile(root / "quad.fadequad", "quad") &&
            TestSupport::WriteFile(root / "red.material", R"({"tint":[0.8,0,0,1]})"),
            "fade fixture assets must be written")) return false;
    Platform::DirectoryContentSource content(root);
    Runtime::Game game{ nullptr, nullptr };
    if (!Expect(game.Initialize(content, {}), "fade fixture project must load")) return false;
    auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "Fading meshes");
    auto* scenePointer = scene.get();
    auto* camera = scene->CreateGameObject("Camera")->AddComponent<Runtime::Camera>();
    camera->SetProjectionMode(Runtime::Camera::ProjectionMode::Orthographic);
    camera->SetOrthographicSize(2);
    camera->SetAspectRatio(1);
    camera->SetClearColor({ 0, 0, 1, 1 });
    scene->CreateGameObject("Ambient")->AddComponent<Runtime::Light>()->SetKind(Runtime::Light::Kind::Ambient);
    std::array<Runtime::MeshRenderer*, 3> meshes;
    std::array<Runtime::Animator*, 3> animators;
    for (std::size_t index = 0; index < meshes.size(); ++index)
    {
        const float x = (static_cast<float>(index) - 1) * 1.25f;
        auto* object = scene->CreateGameObject("Mesh" + std::to_string(index));
        object->GetTransform().SetPosition({ x, .8f, 0 });
        auto* mesh = object->AddComponent<Runtime::MeshRenderer>();
        mesh->SetMesh(Assets::AssetReference("quad.fadequad"));
        mesh->SetMaterial(Assets::AssetReference("red.material"));
        mesh->SetSortingOrder(static_cast<int>(index));
        meshes[index] = mesh;
        object = scene->CreateGameObject("Animator" + std::to_string(index));
        object->GetTransform().SetPosition({ x, -.8f, 0 });
        auto* animator = object->AddComponent<Runtime::Animator>();
        animator->SetMesh(Assets::AssetReference("quad.fadequad", 1));
        animator->SetSkeleton(Assets::AssetReference("quad.fadequad", 2));
        animator->SetClip(Assets::AssetReference("quad.fadequad", 3));
        animator->SetMaterial(Assets::AssetReference("red.material"));
        animator->SetSortingOrder(static_cast<int>(index));
        animator->SetPlaying(false);
        animators[index] = animator;
    }
    static_cast<void>(game.GetSceneManager().AddScene(std::move(scene)));
    SceneRendering::SceneRenderPass frontend{ nullptr };
    bool passed = true;
    const auto backends = TestSupport::SupportedBackends();
    passed &= Expect(!backends.empty(), "mesh fade pixel checks require a graphics backend");
    for (const auto* backend : backends)
    {
        auto device = backend->CreateDevice();
        if (!Expect(device && device->Initialize(Platform::NativeSurface{}),
                "fade graphics device must initialize")) { passed = false; continue; }
        passed &= TestSupport::ForEachUiScale([&](const float scale)
        {
            bool scalePassed = true;
            // Change alpha and pass participation between frames to catch cached pipeline state.
            for (const auto alphas : { std::array{ 1.0f, .5f, 0.0f }, std::array{ .25f, 1.0f, .5f } })
            {
                for (std::size_t index = 0; index < meshes.size(); ++index)
                {
                    meshes[index]->SetColor({ .5f, 1, 1, alphas[index] });
                    animators[index]->SetColor({ .5f, 1, 1, alphas[index] });
                }
                const unsigned int extent = static_cast<unsigned int>(128 * scale);
                game.SetRenderSurfaceSize(static_cast<float>(extent), static_cast<float>(extent));
                Rendering::RenderFrameBuilder builder;
                builder.SetRenderTargetSize({ extent, extent });
                frontend.Collect(game, builder);
                const auto frame = std::move(builder).Build();
                const bool hasInvisible = alphas[2] == 0;
                const auto& opaque = frame.GetDrawPackets(Rendering::RenderPass::Opaque);
                const auto& transparent = frame.GetDrawPackets(Rendering::RenderPass::Transparent);
                scalePassed &= Expect(opaque.size() == 2 && transparent.size() == (hasInvisible ? 2 : 4),
                    "only visible fading meshes must enter the transparent pass; full alpha stays opaque");
                Rendering::CapturedImage image;
                if (!Expect(device->RenderToImage(frame, image) && image.IsValid(),
                        "multiple static and animated meshes must render in one fading frame")) return false;
                for (std::size_t index = 0; index < meshes.size(); ++index)
                {
                    const float alpha = alphas[index];
                    const auto expected = TestSupport::Quantize({
                        Rendering::LinearChannelToSrgb(Rendering::SrgbChannelToLinear(.4f) * alpha), 0,
                        Rendering::LinearChannelToSrgb(1 - alpha), 1 });
                    const auto x = static_cast<unsigned int>((64 + (static_cast<float>(index) - 1) * 40) * scale);
                    for (const float y : { 38.4f, 89.6f })
                    {
                        const auto actual = TestSupport::ReadPixel(image, x, static_cast<unsigned int>(y * scale));
                        const bool matches = PixelNear(actual, expected);
                        if (!matches) std::cerr << backend->id << " fade actual=" << TestSupport::Describe(actual)
                            << " expected=" << TestSupport::Describe(expected) << '\n';
                        scalePassed &= Expect(matches,
                            "static and skinned alpha must blend material times instance tint over the background");
                    }
                }
            }
            return scalePassed;
        });
    }
    static_cast<void>(Serialization::RegisterRuntimeComponentFactories());
    animators[0]->SetSortWithSprites(true);
    const auto saved = Serialization::SceneSerializer::SaveToText(*scenePointer);
    const auto restored = Serialization::SceneSerializer::LoadFromBytes(
        { reinterpret_cast<const std::byte*>(saved.data()), saved.size() }, "Fade.scene", game.GetRuntimeContext());
    const auto* object = restored ? restored->FindGameObject("Animator0") : nullptr;
    const auto* animator = object ? object->GetComponent<Runtime::Animator>() : nullptr;
    passed &= Expect(animator && animator->GetColor() == animators[0]->GetColor() && animator->SortsWithSprites(),
        "animated mesh tint and sprite sorting must survive a scene round trip");
    return passed;
}

static const TestSupport::Registration gMeshFadeTests{
    "BackendImage", "mesh and animator fading tests should pass", RunMeshFadeTests };
