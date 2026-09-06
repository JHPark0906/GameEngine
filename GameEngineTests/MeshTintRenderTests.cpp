#include "MeshTintRenderTests.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Assets/SkinnedMeshData.h"
#include "Math/Matrix.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderColorPolicy.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderFrameBuilder.h"
#include "BackendPixelSupport.h"
#include "TestSupport.h"

using TestSupport::Describe;
using TestSupport::Expect;
using TestSupport::ReadPixel;
using TestSupport::Rgba;
using TestSupport::SupportedBackends;

namespace
{
    constexpr unsigned int ImageWidth = 128;
    constexpr unsigned int ImageHeight = 128;
    constexpr unsigned int SampleX = 64;
    constexpr unsigned int SampleY = 64;
    constexpr float MeshDepth = 2.0f;

    constexpr GameEngine::Math::Color ClearColor{
        64.0f / 255.0f, 128.0f / 255.0f, 192.0f / 255.0f, 1.0f };
    constexpr Rgba MeshTextureColor{ 200, 160, 220, 255 };

    /// <summary>
    /// 곱해도 어느 채널도 0이나 그대로 남지 않는 색이다 — 세 채널이 서로 다른 배율이라, tint가
    /// 안 먹었을 때(흰색과 같은 결과)와 잘못된 채널에 먹었을 때를 둘 다 이 하나의 표본에서
    /// 구별할 수 있다.
    /// </summary>
    constexpr GameEngine::Math::Color TestTint{ 0.85f, 0.5f, 0.2f, 1.0f };

    /// <summary>
    /// 두 API가 같은 값을 살짝 다르게 반올림할 수 있다는 것과, 두 번 그려서 비교하는 이 시험이
    /// float 곱셈을 8비트 양자화 위에 한 번 더 얹는다는 것 둘 다를 흡수하는 허용치다.
    /// </summary>
    constexpr int MaximumChannelDifference = 2;

    [[nodiscard]] std::shared_ptr<const GameEngine::Assets::TextureData> MakeSolidTexture(
        const std::uint64_t id, const Rgba& color)
    {
        auto texture = std::make_shared<GameEngine::Assets::TextureData>();
        texture->id = id;
        texture->width = 8;
        texture->height = 8;
        texture->pixels.resize(texture->GetByteSize());
        for (std::size_t pixel = 0; pixel < texture->pixels.size(); pixel += 4)
        {
            texture->pixels[pixel] = static_cast<std::byte>(color.r);
            texture->pixels[pixel + 1] = static_cast<std::byte>(color.g);
            texture->pixels[pixel + 2] = static_cast<std::byte>(color.b);
            texture->pixels[pixel + 3] = static_cast<std::byte>(color.a);
        }
        return texture;
    }

    /// <summary>화면을 가득 덮는 사각형 하나다. 표본 자리가 어느 draw 안에 있는지 걱정할
    /// 필요가 없도록, 이 시험은 화면 전체를 이 하나로 채운다.</summary>
    [[nodiscard]] std::shared_ptr<GameEngine::Assets::MeshData> BuildQuad()
    {
        using namespace GameEngine;
        auto mesh = std::make_shared<Assets::MeshData>();
        mesh->id = 0x1000'0000'0000'0020ull;
        const auto corner = [](const float x, const float y)
        {
            Assets::MeshVertex vertex;
            vertex.position = { x, y, MeshDepth };
            vertex.normal = { 0.0f, 0.0f, -1.0f };
            vertex.textureCoordinate = { x, y };
            return vertex;
        };
        mesh->vertices = { corner(-2.0f, -2.0f), corner(-2.0f, 2.0f), corner(2.0f, 2.0f), corner(2.0f, -2.0f) };
        mesh->indices = { 0, 1, 2, 0, 2, 3 };
        return mesh;
    }

    /// <summary>왼쪽 절반이 뼈 0에, 오른쪽 절반이 뼈 1에 매인, 화면을 가득 덮는 사각형이다.
    /// SkinnedMeshRenderTests.cpp의 BuildHingeMesh와 같은 모양이되, 뼈는 둘 다 항등으로 둔다 —
    /// 이 시험이 묻는 것은 포즈가 아니라 틴트다.</summary>
    [[nodiscard]] std::shared_ptr<GameEngine::Assets::SkinnedMeshData> BuildSkinnedQuad()
    {
        using namespace GameEngine;
        const auto vertex = [](const float x, const float y, const std::uint32_t boneIndex)
        {
            Assets::SkinnedMeshVertex result;
            result.position = { x, y, MeshDepth };
            result.normal = { 0.0f, 0.0f, -1.0f };
            result.textureCoordinate = { x, y };
            result.boneIndices = { boneIndex, 0, 0, 0 };
            result.boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
            return result;
        };
        auto mesh = std::make_shared<Assets::SkinnedMeshData>();
        mesh->id = 0x1000'0000'0000'0021ull;
        mesh->vertices = {
            vertex(-2.0f, -2.0f, 0), vertex(-2.0f, 2.0f, 0), vertex(0.0f, 2.0f, 0), vertex(0.0f, -2.0f, 0),
            vertex(0.0f, -2.0f, 1), vertex(0.0f, 2.0f, 1), vertex(2.0f, 2.0f, 1), vertex(2.0f, -2.0f, 1)
        };
        mesh->indices = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
        mesh->bounds = Assets::ComputeSkinnedBounds(mesh->vertices);
        return mesh;
    }

    [[nodiscard]] GameEngine::Rendering::RenderFrame BuildMeshFrame(
        const GameEngine::Math::Color& tint)
    {
        using namespace GameEngine;
        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ ImageWidth, ImageHeight });

        Rendering::CameraRenderData camera;
        camera.clearColor = ClearColor;
        camera.view = Math::Matrix4x4::Identity();
        camera.projection = Math::Matrix4x4::CreateOrthographicLeftHanded(4.0f, 4.0f, 0.1f, 100.0f);
        builder.SetCamera(camera);

        builder.AddAmbientLight({ 0.2f, 0.2f, 0.2f, 1.0f });
        Rendering::LightRenderData sun;
        sun.kind = Rendering::LightKind::Directional;
        sun.direction = { 0.0f, 0.0f, 1.0f };
        sun.color = { 0.8f, 0.8f, 0.8f, 1.0f };
        static_cast<void>(builder.AddLight(sun));

        Rendering::MeshDraw draw;
        draw.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Mesh });
        draw.geometry = builder.AddGeometry({ BuildQuad() });
        draw.material = builder.AddMaterial({ MakeSolidTexture(0x5000'0000'0000'0020ull, MeshTextureColor) });
        draw.localToWorld = Math::Matrix4x4::Identity();
        draw.tint = tint;
        static_cast<void>(builder.TryAddDraw(Rendering::RenderPass::Opaque, draw));
        return std::move(builder).Build();
    }

    [[nodiscard]] GameEngine::Rendering::RenderFrame BuildSkinnedMeshFrame(
        const GameEngine::Math::Color& tint)
    {
        using namespace GameEngine;
        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ ImageWidth, ImageHeight });

        Rendering::CameraRenderData camera;
        camera.clearColor = ClearColor;
        camera.view = Math::Matrix4x4::Identity();
        camera.projection = Math::Matrix4x4::CreateOrthographicLeftHanded(4.0f, 4.0f, 0.1f, 100.0f);
        builder.SetCamera(camera);

        builder.AddAmbientLight({ 0.2f, 0.2f, 0.2f, 1.0f });
        Rendering::LightRenderData sun;
        sun.kind = Rendering::LightKind::Directional;
        sun.direction = { 0.0f, 0.0f, 1.0f };
        sun.color = { 0.8f, 0.8f, 0.8f, 1.0f };
        static_cast<void>(builder.AddLight(sun));

        Rendering::SkinnedMeshDraw draw;
        draw.pipeline = builder.AddPipeline({ Rendering::PipelineKind::SkinnedMesh });
        draw.geometry = builder.AddSkinnedGeometry({ BuildSkinnedQuad() });
        draw.material = builder.AddMaterial({ MakeSolidTexture(0x5000'0000'0000'0021ull, MeshTextureColor) });
        draw.localToWorld = Math::Matrix4x4::Identity();
        draw.tint = tint;
        auto boneMatrices = std::make_shared<std::vector<Math::Matrix4x4>>();
        boneMatrices->push_back(Math::Matrix4x4::Identity());
        boneMatrices->push_back(Math::Matrix4x4::Identity());
        draw.boneMatrices = boneMatrices;
        static_cast<void>(builder.TryAddDraw(Rendering::RenderPass::Opaque, draw));
        return std::move(builder).Build();
    }

    /// <summary>
    /// 캡처한 픽셀과 tint를 곱한 기대 색을 비교한다.
    /// 둘 다 저작된 sRGB이므로 먼저 선형 공간으로 변환해 곱하고 다시 sRGB로 인코딩한다.
    /// sRGB 곡선이 비선형이므로 8비트 채널을 직접 곱한 값은 기대 결과가 아니다.
    /// </summary>
    [[nodiscard]] bool SampleMatchesTintedExpectation(
        const Rgba& untinted, const Rgba& tinted, const GameEngine::Math::Color& tint,
        const std::string& what)
    {
        using GameEngine::Rendering::LinearChannelToSrgb;
        using GameEngine::Rendering::SrgbChannelToLinear;

        const std::array<unsigned char, 3> untintedChannels{ untinted.r, untinted.g, untinted.b };
        const std::array<unsigned char, 3> tintedChannels{ tinted.r, tinted.g, tinted.b };
        const std::array<float, 3> tintChannels{ tint.r, tint.g, tint.b };
        int worst = 0;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const float untintedLinear =
                SrgbChannelToLinear(static_cast<float>(untintedChannels[channel]) / 255.0f);
            const float tintLinear = SrgbChannelToLinear(tintChannels[channel]);
            const float expectedLinear = untintedLinear * tintLinear;
            const float expectedSrgb = LinearChannelToSrgb(expectedLinear);
            const auto expected = static_cast<int>(std::lround(expectedSrgb * 255.0f));
            const int difference =
                std::abs(expected - static_cast<int>(tintedChannels[channel]));
            worst = (std::max)(worst, difference);
        }
        if (worst > MaximumChannelDifference)
        {
            std::cerr << "FAILED: " << what << ": untinted=" << Describe(untinted)
                      << ", tinted=" << Describe(tinted) << ", worst channel difference="
                      << worst << " (expected the tinted sample to be the untinted one times "
                      << "the tint color, both converted to linear space first)\n";
            return false;
        }
        return true;
    }
}

bool RunMeshTintRenderTests()
{
    using namespace GameEngine;

    const std::vector<const Rendering::GraphicsBackendDescriptor*> supported = SupportedBackends();
    if (supported.empty())
    {
        std::cout << "  mesh tint render tests skipped: no supported graphics backend\n";
        return true;
    }

    bool passed = true;
    std::vector<Rgba> meshTintedAcrossBackends;
    std::vector<Rgba> skinnedTintedAcrossBackends;
    for (const Rendering::GraphicsBackendDescriptor* const backend : supported)
    {
        const std::string prefix = std::string(backend->id) + ": ";
        const std::unique_ptr<Rendering::IGraphicsDevice> device = backend->CreateDevice();
        if (!device || !device->Initialize(Platform::NativeSurface{}))
        {
            passed &= Expect(
                false, (prefix + "a supported backend should initialize headless").c_str());
            continue;
        }

        // 🔴 Mesh: 흰색으로 한 번, 시험 색으로 한 번 그려 둘을 비교한다.
        Rendering::CapturedImage meshWhite;
        Rendering::CapturedImage meshTinted;
        const bool meshRendered =
            device->RenderToImage(BuildMeshFrame(Math::Color::White), meshWhite) &&
            meshWhite.IsValid() &&
            device->RenderToImage(BuildMeshFrame(TestTint), meshTinted) && meshTinted.IsValid();
        if (!Expect(meshRendered, (prefix + "the mesh tint frames should render").c_str()))
        {
            continue;
        }
        const Rgba meshWhiteSample = ReadPixel(meshWhite, SampleX, SampleY);
        const Rgba meshTintedSample = ReadPixel(meshTinted, SampleX, SampleY);
        passed &= Expect(
            SampleMatchesTintedExpectation(
                meshWhiteSample, meshTintedSample, TestTint, prefix + "mesh"),
            (prefix + "a mesh's tint should multiply what it draws").c_str());
        meshTintedAcrossBackends.push_back(meshTintedSample);

        // SkinnedMesh.hlsl도 같은 tint 비교를 통과해야 한다.
        Rendering::CapturedImage skinnedWhite;
        Rendering::CapturedImage skinnedTinted;
        const bool skinnedRendered =
            device->RenderToImage(BuildSkinnedMeshFrame(Math::Color::White), skinnedWhite) &&
            skinnedWhite.IsValid() &&
            device->RenderToImage(BuildSkinnedMeshFrame(TestTint), skinnedTinted) &&
            skinnedTinted.IsValid();
        if (!Expect(skinnedRendered, (prefix + "the skinned mesh tint frames should render").c_str()))
        {
            continue;
        }
        const Rgba skinnedWhiteSample = ReadPixel(skinnedWhite, SampleX, SampleY);
        const Rgba skinnedTintedSample = ReadPixel(skinnedTinted, SampleX, SampleY);
        passed &= Expect(
            SampleMatchesTintedExpectation(
                skinnedWhiteSample, skinnedTintedSample, TestTint, prefix + "skinned mesh"),
            (prefix + "a skinned mesh's tint should multiply what it draws").c_str());
        skinnedTintedAcrossBackends.push_back(skinnedTintedSample);

        std::cout << "  " << prefix << "mesh untinted=" << Describe(meshWhiteSample)
                  << " tinted=" << Describe(meshTintedSample)
                  << ", skinned untinted=" << Describe(skinnedWhiteSample)
                  << " tinted=" << Describe(skinnedTintedSample) << '\n';
    }

    // 🔴 두 API가 같은 시험 색을 같은 픽셀로 그리는지도 본다 — BackendImageTests.cpp의
    // 백엔드 간 비교와 같은 요구다.
    for (std::size_t index = 1; index < meshTintedAcrossBackends.size(); ++index)
    {
        const int worst = [&]
        {
            const Rgba& a = meshTintedAcrossBackends[0];
            const Rgba& b = meshTintedAcrossBackends[index];
            return (std::max)(
                {std::abs(static_cast<int>(a.r) - static_cast<int>(b.r)),
                 std::abs(static_cast<int>(a.g) - static_cast<int>(b.g)),
                 std::abs(static_cast<int>(a.b) - static_cast<int>(b.b))});
        }();
        passed &= Expect(
            worst <= MaximumChannelDifference,
            "every backend should tint a mesh the same way");
    }
    for (std::size_t index = 1; index < skinnedTintedAcrossBackends.size(); ++index)
    {
        const int worst = [&]
        {
            const Rgba& a = skinnedTintedAcrossBackends[0];
            const Rgba& b = skinnedTintedAcrossBackends[index];
            return (std::max)(
                {std::abs(static_cast<int>(a.r) - static_cast<int>(b.r)),
                 std::abs(static_cast<int>(a.g) - static_cast<int>(b.g)),
                 std::abs(static_cast<int>(a.b) - static_cast<int>(b.b))});
        }();
        passed &= Expect(
            worst <= MaximumChannelDifference,
            "every backend should tint a skinned mesh the same way");
    }
    if (supported.size() < 2)
    {
        std::cout << "  only one backend is supported here, so nothing was compared across "
                     "backends\n";
    }

    return passed;
}

static const TestSupport::Registration gMeshTintRenderTests{
    "BackendImage", "mesh tint render tests should pass", RunMeshTintRenderTests };
