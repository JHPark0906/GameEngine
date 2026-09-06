#include "MaterialRenderTests.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Assets/AssetDatabase.h"
#include "Assets/AssetImporter.h"
#include "Assets/AssetImporterRegistry.h"
#include "Assets/MeshData.h"
#include "Math/Matrix.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderColorPolicy.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/MeshRenderer.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"
#include "SceneRendering/SceneRenderPass.h"
#include "BackendPixelSupport.h"
#include "TestSupport.h"

using TestSupport::Describe;
using TestSupport::Expect;
using TestSupport::RegistryScope;
using TestSupport::ReadFile;
using TestSupport::Rgba;
using TestSupport::SupportedBackends;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{
    constexpr unsigned int ImageSize = 128;
    constexpr unsigned int SampleX = ImageSize / 2;
    constexpr unsigned int SampleY = ImageSize / 2;

    // AnimatorTests.cpp와 같은 1x1 PNG다. 절댓값 색은 이 시험의 요점이 아니다 — 흰 tint로
    // 그린 것과 시험 색 tint로 그린 것의 비율만 본다.
    constexpr unsigned char OnePixelPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0x0F, 0x04, 0x00, 0x09, 0xFB, 0x03, 0xFD, 0xFB, 0x5E, 0x6B, 0x2B, 0x00, 0x00, 0x00, 0x00,
        0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

    constexpr int MaximumChannelDifference = 2;

    /// <summary>
    /// 진짜 FBX 없이 <c>AssetType::Mesh</c> 서브에셋 하나를 내는 시험용 임포터다.
    /// <c>AssetPayloadKeyTests.cpp</c>의 <c>CountingMeshImporter</c>와 같은 요령이다 — 파일
    /// 내용은 신호일 뿐이고, 실제 형상은 이 임포터가 짓는다. bounds를 실제로 계산해 두는 것이
    /// 중요하다: <c>SceneRenderPass</c>의 컬링이 빈 상자를 화면 밖으로 본다.
    /// </summary>
    class QuadMeshImporter final : public GameEngine::Assets::IAssetImporter
    {
    public:
        [[nodiscard]] GameEngine::Assets::AssetType GetAssetType() const override
        {
            return GameEngine::Assets::AssetType::Mesh;
        }

        [[nodiscard]] bool Import(
            const std::filesystem::path&,
            std::span<const std::byte>,
            const GameEngine::Assets::ImportIdentity& identity,
            const GameEngine::Assets::ImportMode mode,
            GameEngine::Assets::ImportedContents& contents,
            std::string&) const override
        {
            contents.subAssets.push_back({ GameEngine::Assets::AssetType::Mesh, "Quad" });
            if (mode == GameEngine::Assets::ImportMode::Structure)
            {
                return true;
            }
            auto mesh = std::make_shared<GameEngine::Assets::MeshData>();
            mesh->id = identity.MakeResourceId(GameEngine::Assets::ResourceIdDomain::Mesh, 0);
            const auto corner = [](const float x, const float y)
            {
                GameEngine::Assets::MeshVertex vertex;
                vertex.position = { x, y, 2.0f };
                vertex.normal = { 0.0f, 0.0f, -1.0f };
                vertex.textureCoordinate = { x, y };
                return vertex;
            };
            // 기본 카메라(높이 10)에 넉넉히 들어가는 2x2 사각형이다.
            mesh->vertices = {
                corner(-1.0f, -1.0f), corner(-1.0f, 1.0f), corner(1.0f, 1.0f), corner(1.0f, -1.0f) };
            mesh->indices = { 0, 1, 2, 0, 2, 3 };
            mesh->bounds = GameEngine::Assets::ComputeBounds(mesh->vertices);
            contents.meshes.push_back(std::move(mesh));
            return true;
        }
    };

    /// <summary>
    /// 프로젝트 하나를 쓰고 스캔한 뒤, 유일한 GameObject의 MeshRenderer가 참조할 재질을 문자열
    /// 하나로 받는다. material 참조가 다르면 결과가 다른 픽셀로 그려지므로, 이 함수를 흰
    /// tint와 시험 tint 각각에 두 번 부른다.
    /// </summary>
    struct RenderedMaterial
    {
        bool rendered = false;
        Rgba sample;
    };

    [[nodiscard]] RenderedMaterial RenderProjectWithMaterial(
        const GameEngine::Rendering::GraphicsBackendDescriptor& backend,
        const std::string& materialReference)
    {
        using namespace GameEngine;

        TemporaryDirectory projectDirectory("material-render");
        const std::filesystem::path root = projectDirectory.GetPath();
        const bool wrote =
            WriteFile(root / "MaterialRenderTest.gameproject", "{}") &&
            WriteFile(root / "Meshes" / "quad.testmaterialmesh", "quad") &&
            WriteFile(root / "Sprites" / "tile.png",
                std::string_view(reinterpret_cast<const char*>(OnePixelPng), sizeof(OnePixelPng))) &&
            WriteFile(root / "Sprites" / "tile.png.meta",
                R"({"format": "gameengine-meta/1", "pixelsPerUnit": 1.0})") &&
            WriteFile(root / "Materials" / "quad_white.material",
                R"({"texture": "Sprites/tile.png"})") &&
            WriteFile(root / "Materials" / "quad_tinted.material",
                R"({"texture": "Sprites/tile.png", "tint": [0.85, 0.5, 0.2, 1.0]})");
        if (!wrote)
        {
            return {};
        }

        const RegistryScope registries;
        static const QuadMeshImporter quadMeshImporter;
        if (!Assets::AssetImporterRegistry::Register(".testmaterialmesh", quadMeshImporter))
        {
            return {};
        }

        const Platform::DirectoryContentSource content(root);
        Runtime::Game game{ nullptr, nullptr };
        if (!game.Initialize(content, {}))
        {
            return {};
        }
        game.SetRenderSurfaceSize(static_cast<float>(ImageSize), static_cast<float>(ImageSize));

        const Assets::AssetReference meshReference =
            Assets::AssetReference::Parse("Meshes/quad.testmaterialmesh");
        auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "MaterialRenderScene");
        Runtime::GameObject* const object = scene->CreateGameObject("Quad");
        Runtime::MeshRenderer* const renderer = object->AddComponent<Runtime::MeshRenderer>();
        renderer->SetMesh(meshReference);
        renderer->SetMaterial(Assets::AssetReference::Parse(materialReference));
        static_cast<void>(game.GetSceneManager().AddScene(std::move(scene)));

        SceneRendering::SceneRenderPass frontend{ nullptr };
        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ ImageSize, ImageSize });
        frontend.Collect(game, builder);
        Rendering::RenderFrame frame = std::move(builder).Build();

        const std::unique_ptr<Rendering::IGraphicsDevice> device = backend.CreateDevice();
        if (!device || !device->Initialize(Platform::NativeSurface{}))
        {
            return {};
        }
        Rendering::CapturedImage image;
        if (!device->RenderToImage(std::move(frame), image) || !image.IsValid())
        {
            return {};
        }
        return { true, TestSupport::ReadPixel(image, SampleX, SampleY) };
    }
}

bool RunMaterialRenderTests()
{
    using namespace GameEngine;

    const std::vector<const Rendering::GraphicsBackendDescriptor*> supported = SupportedBackends();
    if (supported.empty())
    {
        std::cout << "  material render tests skipped: no supported graphics backend\n";
        return true;
    }

    bool passed = true;
    for (const Rendering::GraphicsBackendDescriptor* const backend : supported)
    {
        const std::string prefix = std::string(backend->id) + ": ";

        // Material 에셋 경로: MeshRenderer가 진짜 Material 에셋을 참조해서 그리면 그 tint가
        // 실제로 반영된다. 흰 tint와 시험 tint를 각각 진짜 프로젝트로 그려 비율로 비교한다 —
        // MeshTintRenderTests.cpp가 손으로 지은 MeshDraw에 대해 증명한 그 관계를, 이번에는
        // AssetDatabase·MeshRenderer·SceneRenderPass가 실제로 채운 MeshDraw에 대해 다시 잰다.
        const RenderedMaterial white =
            RenderProjectWithMaterial(*backend, "Materials/quad_white.material");
        const RenderedMaterial tinted =
            RenderProjectWithMaterial(*backend, "Materials/quad_tinted.material");
        if (!Expect(
                white.rendered && tinted.rendered,
                (prefix + "a MeshRenderer with a real Material asset should render").c_str()))
        {
            continue;
        }

        using Rendering::LinearChannelToSrgb;
        using Rendering::SrgbChannelToLinear;
        constexpr GameEngine::Math::Color testTint{ 0.85f, 0.5f, 0.2f, 1.0f };
        int worst = 0;
        const std::array<unsigned char, 3> whiteChannels{ white.sample.r, white.sample.g, white.sample.b };
        const std::array<unsigned char, 3> tintedChannels{ tinted.sample.r, tinted.sample.g, tinted.sample.b };
        const std::array<float, 3> tintChannels{ testTint.r, testTint.g, testTint.b };
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const float whiteLinear =
                SrgbChannelToLinear(static_cast<float>(whiteChannels[channel]) / 255.0f);
            const float tintLinear = SrgbChannelToLinear(tintChannels[channel]);
            const float expectedSrgb = LinearChannelToSrgb(whiteLinear * tintLinear);
            const auto expected = static_cast<int>(std::lround(expectedSrgb * 255.0f));
            worst = (std::max)(worst, std::abs(expected - static_cast<int>(tintedChannels[channel])));
        }
        if (worst > MaximumChannelDifference)
        {
            std::cerr << "FAILED: " << prefix << "a MeshRenderer's Material tint should reach the "
                      << "pixel: white=" << Describe(white.sample) << ", tinted="
                      << Describe(tinted.sample) << ", worst channel difference=" << worst << '\n';
        }
        passed &= Expect(
            worst <= MaximumChannelDifference,
            (prefix + "a MeshRenderer's real Material tint should multiply what it draws").c_str());
        std::cout << "  " << prefix << "material white=" << Describe(white.sample)
                  << " tinted=" << Describe(tinted.sample) << '\n';

        // Material 대신 텍스처를 가리키는 구 형식 참조는 크래시를 일으키지 않고 그리기에서
        // 제외되어야 한다.
        const RenderedMaterial wrongType =
            RenderProjectWithMaterial(*backend, "Sprites/tile.png");
        passed &= Expect(
            wrongType.rendered,
            (prefix + "a project with a mistyped material reference should still initialize and "
                      "render without crashing").c_str());
        // 크래시하지 않는 것으로는 부족하다 — 그 자리가 진짜 건너뛰어졌는지, 즉 아무것도
        // 그리지 않아 카메라의 지우기 색(검정, MakeDefaultCamera)이 그대로 남았는지까지 본다.
        if (wrongType.rendered)
        {
            passed &= Expect(
                wrongType.sample == Rgba{ 0, 0, 0, 255 },
                (prefix + "a mistyped material reference should leave the mesh undrawn, not "
                          "draw it with whatever it happened to resolve to").c_str());
        }
    }

    return passed;
}

static const TestSupport::Registration gMaterialRenderTests{
    "BackendImage", "material render tests should pass", RunMaterialRenderTests };
