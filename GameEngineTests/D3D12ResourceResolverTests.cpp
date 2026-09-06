#include "D3D12ResourceResolverTests.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Assets/TextureData.h"
#include "Platform/NativeSurface.h"
#include "Rendering/D3D12/D3D12GraphicsDevice.h"
#include "Rendering/D3D12/D3D12ResourceResolver.h"
#include "Rendering/RenderFrameBuilder.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine;
    using Rendering::D3D12::D3D12ResolvedTexture;

    /// <summary>같은 id 아래 주소와 제어 블록이 독립적으로 바뀌는 네 가지 조합을 검사한다.</summary>
    template <typename TImage, typename TResolve, typename TPixels>
    [[nodiscard]] bool CheckPixelOwners(
        const TImage& prototype, TResolve&& resolve, TPixels&& pixels, const std::string& prefix)
    {
        bool passed = true;
        const auto check = [&](const std::shared_ptr<const TImage>& owner, const char* scenario)
        {
            const D3D12ResolvedTexture* const resolved = resolve(owner);
            const std::string label = prefix + scenario;
            if (!TestSupport::Expect(resolved != nullptr, (label + " should resolve").c_str()))
            {
                passed = false;
                return;
            }
            passed &= TestSupport::Expect(resolved->revision == owner->revision,
                (label + " should refresh the revision").c_str());
            passed &= TestSupport::Expect(resolved->pixels.get() == &pixels(*owner),
                (label + " should reference the current pixel vector").c_str());
            passed &= TestSupport::Expect(
                !resolved->pixels.owner_before(owner) && !owner.owner_before(resolved->pixels),
                (label + " should retain the current control block").c_str());
        };

        auto first = std::make_shared<TImage>(prototype);
        check(first, "initial image");
        ++first->revision;
        check(first, "unchanged owner with a new revision");
        check(first, "repeated unchanged owner");

        auto replacement = std::make_shared<TImage>(*first);
        ++replacement->revision;
        check(replacement, "replacement image with the same id");

        // 별도 제어 블록도 실제 객체를 소유하도록 캡처한다. 주소만 비교하면 이 교체를 놓친다.
        std::shared_ptr<const TImage> sameAddress(replacement.get(),
            [keepAlive = replacement](const TImage*) { static_cast<void>(keepAlive); });
        check(sameAddress, "same address with a different owner");
        const std::weak_ptr<const TImage> retainedOwner = sameAddress;
        sameAddress.reset();
        passed &= TestSupport::Expect(!retainedOwner.expired(),
            (prefix + "the cache should retain the replacement control block").c_str());

        auto pair = std::make_shared<std::array<TImage, 2>>();
        (*pair)[0] = *replacement;
        (*pair)[1] = *replacement;
        ++(*pair)[1].revision;
        check(std::shared_ptr<const TImage>(pair, &(*pair)[0]), "first subobject alias");
        check(std::shared_ptr<const TImage>(pair, &(*pair)[1]),
            "same owner with a different pixel address");
        passed &= TestSupport::Expect(retainedOwner.expired(),
            (prefix + "replacing an alias should release its old control block").c_str());
        return passed;
    }
}

bool RunD3D12ResourceResolverTests()
{
    using namespace GameEngine;
    using namespace Rendering;
    using namespace Rendering::D3D12;

    if (!D3D12GraphicsDevice::IsHardwareSupported())
    {
        std::cout << "  D3D12 is unsupported; pixel owner tests are skipped\n";
        return true;
    }
    D3D12GraphicsDevice device;
    D3D12ResourceResolver resolver;
    if (!TestSupport::Expect(device.Initialize(Platform::NativeSurface{}),
            "D3D12 pixel owner test device should initialize") ||
        !TestSupport::Expect(resolver.Initialize(device), "D3D12 pixel resolver should initialize"))
    {
        return false;
    }

    Assets::TextureData texture;
    texture.id = 0x5000'0000'0000'0071ull;
    texture.revision = 1;
    texture.width = texture.height = 2;
    texture.pixels.resize(texture.GetByteSize(), std::byte{ 0xff });
    bool passed = CheckPixelOwners(texture,
        [&](const std::shared_ptr<const Assets::TextureData>& image)
        {
            RenderFrameBuilder builder;
            const MaterialHandle material = builder.AddMaterial({ image });
            const RenderFrame frame = std::move(builder).Build();
            return resolver.ResolveMaterial(frame, material);
        },
        [](const Assets::TextureData& image) -> const std::vector<std::byte>& { return image.pixels; },
        "D3D12 material ");

    RasterizedTextImage page;
    page.id = 0x3000'0000'0000'0071ull;
    page.revision = 1;
    page.width = page.height = 2;
    page.alphaPixels.resize(4, std::byte{ 0xff });
    passed &= CheckPixelOwners(page,
        [&](const std::shared_ptr<const RasterizedTextImage>& image)
        {
            TextDraw draw;
            draw.page = image;
            return resolver.ResolveText(draw);
        },
        [](const RasterizedTextImage& image) -> const std::vector<std::byte>&
        {
            return image.alphaPixels;
        },
        "D3D12 text ");
    return passed;
}

static const TestSupport::Registration gD3D12ResourceResolverTests{
    "BackendImage", "D3D12 pixel aliases should follow address and ownership changes",
    RunD3D12ResourceResolverTests };
