#include "MeshSpriteSortingTests.h"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "Assets/AssetImporter.h"
#include "Assets/AssetImporterRegistry.h"
#include "Assets/AssetReference.h"
#include "Assets/MeshData.h"
#include "BackendPixelSupport.h"
#include "Core/ResourceId.h"
#include "Math/Color.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Camera.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Light.h"
#include "Runtime/MeshRenderer.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/Transform.h"
#include "SceneRendering/SceneRenderPass.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Serialization/SceneSerializer.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using TestSupport::Expect;
    constexpr unsigned int ImageSize = 128;
    constexpr unsigned char OnePixelPng[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00,
        0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08,
        0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54,
        0x78, 0x9C, 0x63, 0xF8, 0x0F, 0x04, 0x00, 0x09, 0xFB, 0x03, 0xFD, 0xFB, 0x5E, 0x6B, 0x2B, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

    /// <summary>파일 임포트 이후의 실제 메시/재질/프론트엔드 경로를 통과시키는 사각형이다.</summary>
    class SortingQuadImporter final : public Assets::IAssetImporter
    {
    public:
        [[nodiscard]] Assets::AssetType GetAssetType() const override
        {
            return Assets::AssetType::Mesh;
        }

        [[nodiscard]] bool Import(const std::filesystem::path&, std::span<const std::byte>,
            const Assets::ImportIdentity& identity, const Assets::ImportMode mode,
            Assets::ImportedContents& contents, std::string&) const override
        {
            contents.subAssets.push_back({ Assets::AssetType::Mesh, "Quad" });
            if (mode == Assets::ImportMode::Structure)
            {
                return true;
            }
            auto mesh = std::make_shared<Assets::MeshData>();
            mesh->id = identity.MakeResourceId(Core::ResourceIdDomain::Mesh, 0);
            const auto vertex = [](const float x, const float y)
            {
                Core::MeshVertex result;
                result.position = { x, y, 2.0f };
                result.normal = { 0.0f, 0.0f, -1.0f };
                result.textureCoordinate = { 0.0f, 0.0f };
                return result;
            };
            mesh->vertices = { vertex(-1, -1), vertex(-1, 1), vertex(1, 1), vertex(1, -1) };
            mesh->indices = { 0, 1, 2, 0, 2, 3 };
            mesh->bounds = Assets::ComputeBounds(mesh->vertices);
            contents.meshes.push_back(std::move(mesh));
            return true;
        }
    };

    Runtime::SpriteRenderer* AddSprite(Runtime::Scene& scene, const char* name, const float size,
        const Math::Color& color, const int order)
    {
        auto* object = scene.CreateGameObject(name);
        object->GetTransform().SetPosition({ 0.0f, 0.0f, 1.0f });
        auto* sprite = object->AddComponent<Runtime::SpriteRenderer>();
        sprite->SetSprite(Assets::AssetReference::Parse("white.png"));
        sprite->SetDrawMode(Runtime::SpriteRenderer::DrawMode::Sliced);
        sprite->SetSize({ size, size });
        sprite->SetColor(color);
        sprite->SetSortingOrder(order);
        return sprite;
    }

    [[nodiscard]] bool CheckPixel(const Rendering::CapturedImage& image, const float scale,
        const unsigned int x, const TestSupport::Rgba color, const char* message)
    {
        return Expect(TestSupport::ReadPixel(image, static_cast<unsigned int>(x * scale),
                          static_cast<unsigned int>(64 * scale)) == color,
            message);
    }
}

bool RunMeshSpriteSortingTests()
{
    using namespace GameEngine;
    const TestSupport::RegistryScope registries;
    static const SortingQuadImporter importer;
    if (!Expect(Assets::AssetImporterRegistry::Register(".sortingquad", importer),
            "the sorted mesh fixture importer must register"))
    {
        return false;
    }
    const TestSupport::TemporaryDirectory directory("mesh-sprite-sorting");
    const auto root = directory.GetPath();
    if (!Expect(TestSupport::WriteFile(root / "Sorting.gameproject", "{}") &&
                    TestSupport::WriteFile(root / "cat.sortingquad", "quad") &&
                    TestSupport::WriteFile(root / "cat.material", R"({"tint":[1,0,0,1]})") &&
                    TestSupport::WriteFile(root / "white.png",
                        std::string_view(
                            reinterpret_cast<const char*>(OnePixelPng), sizeof(OnePixelPng))) &&
                    TestSupport::WriteFile(root / "white.png.meta",
                        R"({"format":"gameengine-meta/1","pixelsPerUnit":1.0})"),
            "the sorted mesh fixture assets must be written"))
    {
        return false;
    }
    const Platform::DirectoryContentSource content(root);
    Runtime::Game game{ nullptr, nullptr };
    if (!Expect(game.Initialize(content, {}), "the sorted mesh fixture project must load"))
    {
        return false;
    }
    auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "Mesh sprite sorting");
    auto* scenePointer = scene.get();
    auto* camera = scene->CreateGameObject("Camera")->AddComponent<Runtime::Camera>();
    camera->SetProjectionMode(Runtime::Camera::ProjectionMode::Orthographic);
    camera->SetOrthographicSize(2.0f);
    camera->SetAspectRatio(1.0f);
    scene->CreateGameObject("Ambient")->AddComponent<Runtime::Light>()->SetKind(
        Runtime::Light::Kind::Ambient);
    auto* mesh = scene->CreateGameObject("Cat")->AddComponent<Runtime::MeshRenderer>();
    mesh->SetMesh(Assets::AssetReference::Parse("cat.sortingquad"));
    mesh->SetMaterial(Assets::AssetReference::Parse("cat.material"));
    mesh->SetSortingOrder(30);
    auto* background = AddSprite(*scene, "Background", 4, { 0, 0, 1, 1 }, -1000);
    auto* character = AddSprite(*scene, "Character", 3, { 0, 1, 0, 1 }, 10);
    auto* overlay = AddSprite(*scene, "Overlay", 16, { 1, 0, 1, 1 }, -2000);
    overlay->SetSpace(Runtime::SpriteRenderer::Space::Screen);
    static_cast<void>(game.GetSceneManager().AddScene(std::move(scene)));
    SceneRendering::SceneRenderPass frontend{ nullptr };
    const auto collect = [&](const float scale)
    {
        const unsigned int extent = static_cast<unsigned int>(ImageSize * scale);
        game.SetRenderSurfaceSize(static_cast<float>(extent), static_cast<float>(extent));
        overlay->GetGameObject()->GetTransform().SetPosition({ 64 * scale, 64 * scale, 0 });
        overlay->SetSize({ 16 * scale, 16 * scale });
        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ extent, extent });
        frontend.Collect(game, builder);
        return std::move(builder).Build();
    };
    bool passed =
        Expect(!mesh->SortsWithSprites(), "ordinary meshes must remain opaque by default");
    const auto opaque = collect(1);
    passed &=
        Expect(opaque.GetDraws<Rendering::MeshDraw>(Rendering::RenderPass::Opaque).size() == 1 &&
                   opaque.GetDraws<Rendering::MeshDraw>(Rendering::RenderPass::Transparent).empty(),
            "the default mesh must retain its original opaque pass");
    mesh->SetSortWithSprites(true);
    const auto sorted = collect(1);
    const auto& packets = sorted.GetDrawPackets(Rendering::RenderPass::Transparent);
    passed &= Expect(sorted.Validate().IsValid() &&
                         sorted.GetDrawPackets(Rendering::RenderPass::Opaque).empty() &&
                         packets.size() == 3,
        "a sorted mesh must join the sprite pass without duplicate opaque draws");
    if (packets.size() == 3)
    {
        passed &= Expect(std::holds_alternative<Rendering::SpriteDraw>(packets[0].payload) &&
                             packets[0].sortingOrder == -1000 &&
                             packets[0].instanceId == background->GetInstanceId() &&
                             std::holds_alternative<Rendering::SpriteDraw>(packets[1].payload) &&
                             packets[1].sortingOrder == 10 &&
                             packets[1].instanceId == character->GetInstanceId() &&
                             std::holds_alternative<Rendering::MeshDraw>(packets[2].payload) &&
                             packets[2].sortingOrder == 30 &&
                             packets[2].instanceId == mesh->GetInstanceId(),
            "background, character, and mesh must retain their declared order and identity");
    }
    static_cast<void>(Serialization::RegisterRuntimeComponentFactories());
    const auto saved = Serialization::SceneSerializer::SaveToText(*scenePointer);
    const std::span<const std::byte> bytes(
        reinterpret_cast<const std::byte*>(saved.data()), saved.size());
    const auto restored = Serialization::SceneSerializer::LoadFromBytes(
        bytes, "Sorted.scene", game.GetRuntimeContext());
    const auto* restoredObject = restored ? restored->FindGameObject("Cat") : nullptr;
    const auto* restoredMesh =
        restoredObject ? restoredObject->GetComponent<Runtime::MeshRenderer>() : nullptr;
    passed &= Expect(
        restoredMesh && restoredMesh->SortsWithSprites() && restoredMesh->GetSortingOrder() == 30,
        "scene serialization must preserve sprite sorting and mesh order");
    const auto backends = TestSupport::SupportedBackends();
    passed &= Expect(!backends.empty(), "mesh sorting pixel checks require a graphics backend");
    for (const auto* backend : backends)
    {
        auto device = backend->CreateDevice();
        if (!Expect(device && device->Initialize(Platform::NativeSurface{}),
                "the mesh sorting graphics device must initialize"))
        {
            passed = false;
            continue;
        }
        passed &= TestSupport::ForEachUiScale(
            [&](const float scale)
            {
                bool scalePassed = true;
                for (const int meshOrder : { 30, 5 })
                {
                    mesh->SetSortingOrder(meshOrder);
                    Rendering::CapturedImage image;
                    if (!Expect(device->RenderToImage(collect(scale), image) && image.IsValid(),
                            "each backend must render the mixed mesh and sprite frame"))
                    {
                        return false;
                    }
                    const TestSupport::Rgba covered = meshOrder == 30
                                                          ? TestSupport::Rgba{ 255, 0, 0, 255 }
                                                          : TestSupport::Rgba{ 0, 255, 0, 255 };
                    scalePassed &= CheckPixel(image, scale, 88, covered,
                        "changing mesh sorting order must move it before or after the character "
                        "sprite");
                    scalePassed &= CheckPixel(image, scale, 108, { 0, 255, 0, 255 },
                        "the character must cover the background outside the mesh");
                    scalePassed &= CheckPixel(image, scale, 120, { 0, 0, 255, 255 },
                        "the background must remain visible outside the character");
                    scalePassed &= CheckPixel(image, scale, 64, { 255, 0, 255, 255 },
                        "screen overlay must cover both mesh and character regardless of its "
                        "sorting order");
                }
                return scalePassed;
            });
        std::cout << "  " << backend->id << ": mixed mesh and sprite ordering checked\n";
    }
    mesh->SetSortWithSprites(false);
    passed &=
        Expect(collect(1).GetDraws<Rendering::MeshDraw>(Rendering::RenderPass::Opaque).size() == 1,
            "turning the option off must return the mesh to the ordinary opaque pass");
    return passed;
}

static const TestSupport::Registration gMeshSpriteSortingTests{ "BackendImage",
    "mesh sprite sorting should preserve draw order and opaque defaults",
    RunMeshSpriteSortingTests };
