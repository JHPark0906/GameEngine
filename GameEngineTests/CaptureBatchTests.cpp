#include "CaptureBatchTests.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "App/RenderThread.h"
#include "App/RenderSubmission.h"
#include "Assets/TextureData.h"
#include "Math/Color.h"
#include "Math/Matrix.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderFrameBuilder.h"
#include "BackendPixelSupport.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using TestSupport::Expect;
    using TestSupport::ReadPixel;
    using TestSupport::Rgba;
    using BatchItem = Rendering::IGraphicsDevice::CaptureBatchItem;

    [[nodiscard]] Rendering::RenderFrame MakeFrame(
        const Rendering::RenderTargetSize size, const Math::Color color)
    {
        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize(size);
        Rendering::CameraRenderData camera;
        camera.view = Math::Matrix4x4::Identity();
        camera.projection = Math::Matrix4x4::Identity();
        camera.clearColor = color;
        builder.SetCamera(camera);
        return std::move(builder).Build();
    }

    [[nodiscard]] Rendering::RenderFrame MakeTexturedFrame(
        const Rendering::RenderTargetSize size, const Rgba color, const std::uint64_t revision)
    {
        auto texture = std::make_shared<Assets::TextureData>();
        texture->id = 0x4000'0000'0000'BA7Cull;
        texture->revision = revision;
        texture->width = 8;
        texture->height = 8;
        texture->pixels.resize(texture->GetByteSize());
        for (std::size_t offset = 0; offset < texture->pixels.size(); offset += 4)
        {
            texture->pixels[offset] = static_cast<std::byte>(color.r);
            texture->pixels[offset + 1] = static_cast<std::byte>(color.g);
            texture->pixels[offset + 2] = static_cast<std::byte>(color.b);
            texture->pixels[offset + 3] = static_cast<std::byte>(color.a);
        }

        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize(size);
        Rendering::CameraRenderData camera;
        camera.view = Math::Matrix4x4::Identity();
        camera.projection = Math::Matrix4x4::CreateOrthographicLeftHanded(4.0f, 4.0f, 0.1f, 100.0f);
        builder.SetCamera(camera);
        Rendering::SpriteDraw sprite;
        sprite.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Sprite });
        sprite.material = builder.AddMaterial({ std::move(texture) });
        sprite.localToWorld = Math::Matrix4x4::CreateTranslation({ 0.0f, 0.0f, 1.0f });
        sprite.pixelsPerUnit = 4.0f;
        static_cast<void>(builder.TryAddDraw(Rendering::RenderPass::Transparent, sprite, 0, 1));
        return std::move(builder).Build();
    }

    [[nodiscard]] bool HasCenterPixel(
        const Rendering::CapturedImage& image, const Rendering::RenderTargetSize size,
        const Rgba color, const std::string& description)
    {
        return Expect(image.IsValid() && image.width == size.width && image.height == size.height,
                   (description + ": current capture dimensions should be preserved").c_str()) &&
            Expect(ReadPixel(image, size.width / 2, size.height / 2) == color,
                (description + ": pixels should belong to this request, not a preceding capture").c_str());
    }

    // Implements only the original single-image operation. The inherited batch must keep
    // existing devices usable and must never request the previous frame's pixels.
    class FallbackDevice final : public Rendering::IGraphicsDevice
    {
    public:
        bool Initialize(const Platform::NativeSurface&) override { return true; }
        bool BeginFrame() override { return true; }
        Rendering::RenderTargetSize GetRenderTargetSize() const override { return { 1, 1 }; }
        Rendering::GraphicsDeviceCapabilities GetCapabilities() const override { return {}; }
        bool Render(const Rendering::RenderFrame&) override { ++mFrames; return true; }
        bool EndFrame() override { return true; }
        bool RenderToImage(const Rendering::RenderFrame& frame, Rendering::CapturedImage& image,
            const CaptureRequest& request) override
        {
            mChannels.push_back(request.channel);
            mAnyDeferred = mAnyDeferred || request.deferred;
            image = {};
            const unsigned int width = frame.GetRenderTargetSize().width;
            if (width == 0) return false;
            image.width = 1;
            image.height = 1;
            image.pixels.assign(Rendering::CapturedImage::BytesPerPixel, static_cast<std::byte>(width));
            return true;
        }

        std::vector<unsigned int> mChannels;
        bool mAnyDeferred = false;
        unsigned int mFrames = 0;
    };

    [[nodiscard]] bool CheckCurrentImages(Rendering::IGraphicsDevice& device, const std::string& prefix)
    {
        constexpr std::array<Rgba, 4> colors{
            Rgba{ 255, 0, 0, 255 }, Rgba{ 0, 255, 0, 255 },
            Rgba{ 0, 0, 255, 255 }, Rgba{ 255, 255, 0, 255 } };
        bool passed = true;
        for (unsigned int frame = 0; frame < 8; ++frame)
        {
            // Stable sizes exercise overlap; the later change also replaces capture targets.
            const Rendering::RenderTargetSize firstSize{ frame < 4 ? 35u : 67u, 23 };
            const Rendering::RenderTargetSize secondSize{ frame < 4 ? 67u : 35u, 29 };
            const Rgba firstColor = colors[frame % colors.size()];
            const Rgba secondColor = colors[(frame + 1) % colors.size()];
            // Both frames deliberately name one texture id with different owners/revisions.
            // Its second queued update must not change pixels captured by the first draw.
            const Rendering::RenderFrame first = MakeTexturedFrame(firstSize, firstColor, frame * 2 + 1);
            const Rendering::RenderFrame second = MakeTexturedFrame(secondSize, secondColor, frame * 2 + 2);
            Rendering::CapturedImage firstImage;
            Rendering::CapturedImage secondImage;
            std::array<BatchItem, 2> items{ BatchItem{ first, firstImage, 0 }, { second, secondImage, 1 } };
            device.RenderToImages(items);
            passed &= Expect(items[0].succeeded && items[1].succeeded,
                (prefix + "both current images should succeed on the first batch and every later batch").c_str());
            passed &= HasCenterPixel(firstImage, firstSize, firstColor, prefix + "first view");
            passed &= HasCenterPixel(secondImage, secondSize, secondColor, prefix + "second view");
        }
        return passed;
    }

    [[nodiscard]] bool CheckSequentialAliases(Rendering::IGraphicsDevice& device, const std::string& prefix)
    {
        const Rendering::RenderFrame red = MakeFrame({ 31, 19 }, Math::Color{ 1, 0, 0, 1 });
        const Rendering::RenderFrame green = MakeFrame({ 47, 21 }, Math::Color{ 0, 1, 0, 1 });
        const Rendering::RenderFrame blue = MakeFrame({ 63, 25 }, Math::Color{ 0, 0, 1, 1 });
        std::array<Rendering::CapturedImage, 3> images;
        std::array<BatchItem, 3> repeated{
            BatchItem{ red, images[0], 2 }, { green, images[1], 2 }, { blue, images[2], 2 } };
        device.RenderToImages(repeated);
        bool passed = Expect(repeated[0].succeeded && repeated[1].succeeded && repeated[2].succeeded,
            (prefix + "three captures of one channel should preserve sequential behavior").c_str());
        passed &= HasCenterPixel(images[0], { 31, 19 }, { 255, 0, 0, 255 }, prefix + "duplicate first");
        passed &= HasCenterPixel(images[1], { 47, 21 }, { 0, 255, 0, 255 }, prefix + "duplicate second");
        passed &= HasCenterPixel(images[2], { 63, 25 }, { 0, 0, 255, 255 }, prefix + "duplicate third");

        Rendering::CapturedImage aliased;
        std::array<BatchItem, 2> sameOutput{ BatchItem{ red, aliased, 0 }, { green, aliased, 1 } };
        device.RenderToImages(sameOutput);
        passed &= Expect(sameOutput[0].succeeded && sameOutput[1].succeeded,
            (prefix + "two successful calls may share their output image").c_str());
        passed &= HasCenterPixel(aliased, { 47, 21 }, { 0, 255, 0, 255 }, prefix + "last aliased output");

        const Rendering::RenderFrame invalid = MakeFrame({ 0, 19 }, Math::Color::Black);
        std::array<BatchItem, 2> failedLast{ BatchItem{ red, aliased, 0 }, { invalid, aliased, 1 } };
        device.RenderToImages(failedLast);
        passed &= Expect(failedLast[0].succeeded && !failedLast[1].succeeded && !aliased.IsValid(),
            (prefix + "a failed last aliased capture should clear the preceding image").c_str());
        return passed;
    }

    [[nodiscard]] bool CheckPartialFailure(Rendering::IGraphicsDevice& device, const std::string& prefix)
    {
        const Rendering::RenderFrame invalid = MakeFrame({ 0, 17 }, Math::Color::Black);
        const Rendering::RenderFrame valid = MakeFrame({ 37, 17 }, Math::Color{ 0, 0, 1, 1 });
        Rendering::CapturedImage badImage;
        Rendering::CapturedImage goodImage;
        std::array<BatchItem, 2> items{ BatchItem{ invalid, badImage, 0, true }, { valid, goodImage, 1 } };
        device.RenderToImages(items);
        bool passed = Expect(!items[0].succeeded && !badImage.IsValid() && items[1].succeeded,
            (prefix + "an invalid first item should not stop the first valid headless capture").c_str());
        passed &= HasCenterPixel(goodImage, { 37, 17 }, { 0, 0, 255, 255 }, prefix + "after invalid frame");

        // Channel support is backend-specific. Compare the batch result with its single-call
        // contract, including D3D12's out-of-range rejection and D3D11's arbitrary channel keys.
        constexpr unsigned int invalidChannel = (std::numeric_limits<unsigned int>::max)();
        Rendering::CapturedImage single;
        const bool expected = device.RenderToImage(valid, single, { invalidChannel, false });
        std::array<BatchItem, 2> badChannel{
            BatchItem{ valid, badImage, invalidChannel }, { valid, goodImage, 1 } };
        device.RenderToImages(badChannel);
        passed &= Expect(badChannel[0].succeeded == expected && badImage.IsValid() == single.IsValid() &&
                badChannel[1].succeeded,
            (prefix + "invalid-channel behavior should match the single capture and preserve other results").c_str());

        std::array<BatchItem, 2> retry{ BatchItem{ valid, badImage, 0 }, { valid, goodImage, 1 } };
        device.RenderToImages(retry);
        passed &= Expect(retry[0].succeeded && retry[1].succeeded,
            (prefix + "valid captures should work after a failed batch item").c_str());
        passed &= HasCenterPixel(badImage, { 37, 17 }, { 0, 0, 255, 255 }, prefix + "retry first");
        passed &= HasCenterPixel(goodImage, { 37, 17 }, { 0, 0, 255, 255 }, prefix + "retry second");
        return passed;
    }
}

bool RunCaptureBatchFallbackTests()
{
    FallbackDevice device;
    const Rendering::RenderFrame first = MakeFrame({ 7, 1 }, Math::Color::Black);
    const Rendering::RenderFrame invalid = MakeFrame({ 0, 1 }, Math::Color::Black);
    const Rendering::RenderFrame last = MakeFrame({ 9, 1 }, Math::Color::Black);
    std::array<Rendering::CapturedImage, 3> images;
    std::array<BatchItem, 3> items{
        BatchItem{ first, images[0], 4 }, { invalid, images[1], 4, true }, { last, images[2], 6 } };
    device.RenderToImages(items);
    bool passed = Expect(items[0].succeeded && !items[1].succeeded && items[2].succeeded &&
            !images[1].IsValid() && device.mChannels == std::vector<unsigned int>{ 4, 4, 6 } && !device.mAnyDeferred,
        "the default batch should preserve immediate call order and independent failures");
    device.mChannels.clear();
    {
        App::RenderThread renderThread(device);
        renderThread.Start();
        std::vector<App::CaptureJob> jobs{ { first, 2 }, { invalid, 3 }, { last, 5 } };
        passed &= Expect(renderThread.Submit({ std::move(jobs), first }),
            "the render thread should accept a capture batch");
        renderThread.WaitUntilIdle();
        std::vector<App::CapturedView> views;
        renderThread.TakeCapturedViews(views);
        renderThread.Stop();
        passed &= Expect(views.size() == 2 && views[0].channel == 2 && views[1].channel == 5 &&
                views[0].image.IsValid() && views[1].image.IsValid() &&
                views[0].image.pixels.front() == std::byte{ 7 } &&
                views[1].image.pixels.front() == std::byte{ 9 } && device.mFrames == 1 && !device.mAnyDeferred,
            "the render thread should publish this batch's successful current images before becoming idle");
    }
    return passed;
}

bool RunCaptureBatchImageTests()
{
    const auto backends = TestSupport::SupportedBackends();
    if (backends.empty())
    {
        std::cout << "  capture batch image tests skipped: no supported graphics backend\n";
        return true;
    }
    bool passed = true;
    for (const Rendering::GraphicsBackendDescriptor* const backend : backends)
    {
        const std::string prefix = std::string(backend->id) + " capture batch: ";
        const auto device = backend->CreateDevice();
        if (!device || !device->Initialize(Platform::NativeSurface{}))
        {
            passed &= TestSupport::Expect(false, (prefix + "headless initialization should succeed").c_str());
            continue;
        }
        passed &= CheckCurrentImages(*device, prefix);
        passed &= CheckSequentialAliases(*device, prefix);
        passed &= CheckPartialFailure(*device, prefix);
    }
    return passed;
}

static const TestSupport::Registration gCaptureBatchFallbackTests{
    "AppTiming", "capture batch fallback tests should pass", RunCaptureBatchFallbackTests };

static const TestSupport::Registration gCaptureBatchImageTests{
    "BackendImage", "capture batch image tests should pass", RunCaptureBatchImageTests };
