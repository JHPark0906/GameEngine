#include "BackendBindingCacheTests.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Assets/SkinnedMeshData.h"
#include "Math/Matrix.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderFrameBuilder.h"
#include "BackendPixelSupport.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using namespace Rendering;
    using TestSupport::Expect;
    using TestSupport::Rgba;

    constexpr Rgba Red{ 255, 0, 0, 255 };
    constexpr Rgba Blue{ 0, 0, 255, 255 };

    [[nodiscard]] std::shared_ptr<const Assets::SkinnedMeshData> MakeQuad()
    {
        const auto vertex = [](const float x, const float y)
        {
            Core::SkinnedMeshVertex result;
            result.position = { x, y, 0.0f };
            result.normal = { 0.0f, 0.0f, -1.0f };
            result.textureCoordinate = { 0.5f, 0.5f };
            result.boneIndices = { 0, 0, 0, 0 };
            result.boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
            return result;
        };
        auto mesh = std::make_shared<Assets::SkinnedMeshData>();
        mesh->id = 0x1000'0000'0000'0072ull;
        mesh->vertices = {
            vertex(-0.375f, -0.375f), vertex(-0.375f, 0.375f),
            vertex(0.375f, 0.375f), vertex(0.375f, -0.375f) };
        mesh->indices = { 0, 1, 2, 0, 2, 3 };
        mesh->bounds = Assets::ComputeSkinnedBounds(mesh->vertices);
        return mesh;
    }

    /// <summary>
    /// 같은 텍스처를 Sprite→SkinnedMesh→Sprite로 넘긴다. 서로 다른 루트 시그니처가 같은 SRV
    /// 슬롯을 다시 받아야 하며, 아래 줄의 Blue→Blue→Red는 반복 바인딩과 텍스처 교체를 나눈다.
    /// 화면 밖 draw는 픽셀을 바꾸지 않고 상수 저장소가 64 KiB를 넘어 자라게 한다.
    /// </summary>
    [[nodiscard]] RenderFrame MakeFrame(const bool growConstants)
    {
        RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ 128, 128 });
        CameraRenderData camera;
        camera.clearColor = Math::Color::Black;
        camera.view = Math::Matrix4x4::Identity();
        camera.projection = Math::Matrix4x4::CreateOrthographicLeftHanded(4.0f, 4.0f, 0.1f, 100.0f);
        builder.SetCamera(camera);
        builder.AddAmbientLight(Math::Color::White);
        const MaterialHandle red = builder.AddMaterial({
            TestSupport::MakeHalvedTexture(0x5000'0000'0000'0072ull, Red, Red) });
        const MaterialHandle blue = builder.AddMaterial({
            TestSupport::MakeHalvedTexture(0x5000'0000'0000'0073ull, Blue, Blue) });
        const PipelineHandle spritePipeline = builder.AddPipeline({ PipelineKind::Sprite });
        const auto sprite = [&](const MaterialHandle material, const float x, const float y,
            const int order)
        {
            SpriteDraw draw;
            draw.pipeline = spritePipeline;
            draw.material = material;
            draw.pixelsPerUnit = 8.0f / 0.75f;
            draw.localToWorld = Math::Matrix4x4::CreateTranslation({ x, y, 1.0f });
            static_cast<void>(builder.TryAddDraw(RenderPass::Transparent, draw, order));
        };
        sprite(red, -1.0f, 0.5f, 0);
        SkinnedMeshDraw skinned;
        skinned.pipeline = builder.AddPipeline({ PipelineKind::SkinnedMesh });
        skinned.geometry = builder.AddSkinnedGeometry({ MakeQuad() });
        skinned.material = red;
        skinned.localToWorld = Math::Matrix4x4::CreateTranslation({ 0.0f, 0.5f, 1.0f });
        skinned.boneMatrices = std::make_shared<std::vector<Math::Matrix4x4>>(
            1, Math::Matrix4x4::Identity());
        static_cast<void>(builder.TryAddDraw(RenderPass::Transparent, skinned, 1));
        sprite(red, 1.0f, 0.5f, 2);
        sprite(blue, -1.0f, -0.5f, 3);
        sprite(blue, 0.0f, -0.5f, 4);
        sprite(red, 1.0f, -0.5f, 5);
        if (growConstants)
        {
            for (int index = 0; index < 300; ++index)
            {
                sprite(red, 100.0f, 0.0f, 6 + index);
            }
        }
        return std::move(builder).Build();
    }

    [[nodiscard]] bool CheckImage(const CapturedImage& image, const std::string& prefix)
    {
        if (!Expect(image.IsValid() && image.width == 128 && image.height == 128,
                (prefix + " should produce a complete image").c_str())) return false;
        bool passed = true;
        constexpr std::array<unsigned int, 3> columns{ 32, 64, 96 };
        for (std::size_t column = 0; column < columns.size(); ++column)
        {
            passed &= Expect(TestSupport::ReadPixel(image, columns[column], 48) == Red,
                (prefix + " should retain red across root signature changes, column " +
                    std::to_string(column)).c_str());
            const Rgba expected = column < 2 ? Blue : Red;
            passed &= Expect(TestSupport::ReadPixel(image, columns[column], 80) == expected,
                (prefix + " should retain and switch the sprite texture, column " +
                    std::to_string(column)).c_str());
        }
        return passed;
    }
}

bool RunBackendBindingCacheTests()
{
    const auto supported = TestSupport::SupportedBackends();
    if (supported.empty())
    {
        std::cout << "  binding cache tests skipped: no supported graphics backend\n";
        return true;
    }
    const RenderFrame small = MakeFrame(false);
    const RenderFrame large = MakeFrame(true);
    bool passed = true;
    for (const GraphicsBackendDescriptor* const backend : supported)
    {
        const std::string prefix = std::string(backend->id) + " binding cache";
        auto device = backend->CreateDevice();
        if (!Expect(device && device->Initialize(Platform::NativeSurface{}),
                (prefix + " device should initialize").c_str()))
        {
            passed = false;
            continue;
        }
        // 두 슬롯 모두 작은 버퍼에서 시작한 뒤 커지고, 다시 작은 프레임을 받아야 한다.
        for (unsigned int frame = 0; frame < 6; ++frame)
        {
            CapturedImage image;
            const RenderFrame& source = frame >= 2 && frame < 4 ? large : small;
            const std::string label = prefix + " immediate frame " + std::to_string(frame);
            if (!Expect(device->RenderToImage(source, image), (label + " should render").c_str()))
            {
                passed = false;
                continue;
            }
            passed &= CheckImage(image, label);
        }
        // 독립 캡처 채널을 번갈아 쓰면 command list가 같아도 슬롯과 루트 상태를 재사용할 수 없다.
        for (unsigned int frame = 0; frame < 6; ++frame)
        for (unsigned int channel = 1; channel <= 2; ++channel)
        {
            CapturedImage image;
            const RenderFrame& source = frame >= 2 && frame < 4 ? large : small;
            const bool captured = device->RenderToImage(source, image, { channel, true });
            if (frame == 0 && !captured) continue;
            const std::string label = prefix + " deferred channel " + std::to_string(channel) +
                " frame " + std::to_string(frame);
            if (!Expect(captured, (label + " should render").c_str()))
            {
                passed = false;
                continue;
            }
            passed &= CheckImage(image, label);
        }
    }
    return passed;
}

static const TestSupport::Registration gBackendBindingCacheTests{
    "BackendImage", "texture bindings should survive root changes and constant buffer growth",
    RunBackendBindingCacheTests };
