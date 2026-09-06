#include "DynamicTextureSnapshotTests.h"

#include <array>
#include <cstddef>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "Assets/TextureData.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Input.h"
#include "UI/UIContext.h"
#include "BackendPixelSupport.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using TestSupport::Expect;
    using TestSupport::Rgba;

    Rendering::RenderFrame BuildImageFrame(UI::UIContext& ui,
        const std::shared_ptr<Assets::TextureData>& texture, const unsigned int extent)
    {
        Runtime::Input input;
        ui.BeginFrame(input, { extent, extent });
        ui.DrawImage({ 0.0f, 0.0f, static_cast<float>(extent), static_cast<float>(extent) }, texture);
        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ extent, extent });
        ui.EndFrame(builder);
        return std::move(builder).Build();
    }
}

bool RunDynamicTextureSnapshotTests()
{
    const auto backends = TestSupport::SupportedBackends();
    return TestSupport::ForEachUiScale([&](const float scale)
    {
        UI::UIContext ui(nullptr, nullptr);
        ui.SetScale(scale);
        const auto extent = static_cast<unsigned int>(64 * scale);
        const std::vector<std::byte> red{
            std::byte{ 255 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 255 } };
        const std::vector<std::byte> blue{
            std::byte{ 0 }, std::byte{ 0 }, std::byte{ 255 }, std::byte{ 255 } };
        std::shared_ptr<Assets::TextureData> current;
        ui.UpdateDynamicTexture(current, 1, 1, red);
        const auto original = current;
        const auto redFrame = BuildImageFrame(ui, current, extent);
        const auto& packet = redFrame.GetDrawPackets(Rendering::RenderPass::Transparent).at(0);
        const auto& draw = std::get<Rendering::SpriteDraw>(packet.payload);
        const auto published = redFrame.GetMaterial(draw.material)->baseColorTexture;

        ui.UpdateDynamicTexture(current, 1, 1, blue);
        bool passed = Expect(published->pixels == red && published->revision == 1,
            "updating a dynamic texture must preserve the pixels and revision of a published frame");
        passed &= Expect(current != published && current->id == published->id && current->revision == 2,
            "a new CPU snapshot should retain the same logical GPU identity with a new revision");
        const auto blueFrame = BuildImageFrame(ui, current, extent);
        const auto beforeInvalid = current;
        ui.UpdateDynamicTexture(current, 0, 1, blue);
        ui.UpdateDynamicTexture(current, 1, 1, {});
        passed &= Expect(current == beforeInvalid, "invalid updates must preserve the current snapshot");
        const std::vector<std::byte> wideRed{
            std::byte{ 255 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 255 },
            std::byte{ 255 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 255 } };
        ui.UpdateDynamicTexture(current, 2, 1, wideRed);
        passed &= Expect(current->id != original->id && current->revision == 1 &&
                beforeInvalid->pixels == blue && original->pixels == red,
            "resizing must issue a new identity without changing either older snapshot");

        for (const auto* backend : backends)
        {
            auto device = backend->CreateDevice();
            if (!Expect(device && device->Initialize(Platform::NativeSurface{}),
                    "a dynamic snapshot test device should initialize"))
            {
                passed = false;
                continue;
            }
            Rendering::CapturedImage redImage, blueImage;
            std::array<Rendering::IGraphicsDevice::CaptureBatchItem, 2> batch{
                Rendering::IGraphicsDevice::CaptureBatchItem{ redFrame, redImage, 0 },
                Rendering::IGraphicsDevice::CaptureBatchItem{ blueFrame, blueImage, 1 } };
            device->RenderToImages(batch);
            passed &= Expect(batch[0].succeeded && redImage.IsValid() &&
                    TestSupport::ReadPixel(redImage, extent / 2, extent / 2) == Rgba{ 255, 0, 0, 255 },
                "an older UI frame must render its red snapshot after a newer update was published");
            passed &= Expect(batch[1].succeeded && blueImage.IsValid() &&
                    TestSupport::ReadPixel(blueImage, extent / 2, extent / 2) == Rgba{ 0, 0, 255, 255 },
                "the same texture identity must render the newer blue snapshot in another capture");
            Rendering::CapturedImage revisited;
            passed &= Expect(device->RenderToImage(redFrame, revisited) && revisited.IsValid() &&
                    TestSupport::ReadPixel(revisited, extent / 2, extent / 2) == Rgba{ 255, 0, 0, 255 },
                "a retained older revision must still render correctly after the newer revision");
        }
        return passed;
    });
}

static const TestSupport::Registration gDynamicTextureSnapshotTests{
    "BackendImage", "Published dynamic texture snapshots should remain immutable", RunDynamicTextureSnapshotTests };
