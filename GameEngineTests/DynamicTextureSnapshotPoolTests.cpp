#include "DynamicTextureSnapshotPoolTests.h"

#include <array>
#include <cstddef>
#include <latch>
#include <memory>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "Assets/TextureData.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Input.h"
#include "UI/DynamicTextureSnapshotPool.h"
#include "UI/UIContext.h"
#include "TestSupport.h"

namespace
{
using namespace GameEngine;
using TestSupport::Expect;
using UI::Detail::DynamicTextureSnapshotPool;

std::shared_ptr<Assets::TextureData> MakeSnapshot(DynamicTextureSnapshotPool& pool,
    const std::size_t bytes, const std::byte value = std::byte{ 17 })
{
    auto writable = pool.Acquire(bytes);
    writable->id = 42;
    writable->revision = 7;
    writable->width = static_cast<unsigned int>(bytes / 4);
    writable->height = 1;
    writable->pixels.assign(bytes, value);
    return pool.Publish(std::move(writable));
}

bool ReuseRequiresFinalSharedRelease()
{
    DynamicTextureSnapshotPool pool;
    auto snapshot = MakeSnapshot(pool, 1024);
    const auto* originalAddress = snapshot.get();
    const auto* originalPixels = snapshot->pixels.data();
    std::weak_ptr<Assets::TextureData> oldOwner = snapshot;
    std::shared_ptr<const std::vector<std::byte>> pixelAlias(snapshot, &snapshot->pixels);
    snapshot.reset();
    auto other = pool.Acquire(1024);
    other->pixels.assign(1024, std::byte{ 34 });
    if (!Expect(pool.GetRetention().entries == 0 && other.get() != originalAddress &&
        pixelAlias->front() == std::byte{ 17 } && oldOwner.lock()->revision == 7,
        "an aliased backend pixel owner must keep its published snapshot immutable and unavailable")) return false;
    pixelAlias.reset();
    if (!Expect(oldOwner.expired() && pool.GetRetention().entries == 1,
        "the final shared release must return exactly one exclusive snapshot")) return false;
    auto reused = pool.Acquire(1024);
    if (!Expect(reused.get() == originalAddress && reused->pixels.data() == originalPixels &&
        reused->pixels.empty() && reused->id == 0 && reused->revision == 0 &&
        pool.GetRetention().entries == 0,
        "acquisition must reuse returned pixel capacity and reset payload metadata")) return false;
    reused->id = 99;
    reused->pixels.assign(1024, std::byte{ 51 });
    auto publishedAgain = pool.Publish(std::move(reused));
    return Expect(oldOwner.expired() && !oldOwner.lock() &&
        (oldOwner.owner_before(publishedAgain) || publishedAgain.owner_before(oldOwner)),
        "recycled storage must receive a new control block without reviving an old weak owner");
}

bool CrossThreadFinalRelease()
{
    DynamicTextureSnapshotPool pool;
    auto snapshot = MakeSnapshot(pool, 4096);
    const auto* originalAddress = snapshot.get();
    std::weak_ptr<Assets::TextureData> weak = snapshot;
    std::latch started(1);
    std::latch release(1);
    std::thread renderThread([owned = std::move(snapshot), &started, &release]() mutable
    {
        started.count_down();
        release.wait();
        owned.reset();
    });
    started.wait();
    auto duringRead = pool.Acquire(4096);
    const bool exclusive = duringRead.get() != originalAddress && pool.GetRetention().entries == 0;
    release.count_down();
    renderThread.join();
    auto returned = pool.Acquire(4096);
    if (!Expect(exclusive && weak.expired() && returned.get() == originalAddress,
        "a render thread's final release must make storage reusable only after it stops reading")) return false;

    std::array<std::shared_ptr<Assets::TextureData>, 16> frames;
    for (auto& frame : frames) frame = MakeSnapshot(pool, 4096);
    std::latch raceStart(1);
    std::thread releaser([owned = std::move(frames), &raceStart]() mutable
    {
        raceStart.wait();
        for (auto& frame : owned) frame.reset();
    });
    raceStart.count_down();
    bool contentsPreserved = true;
    for (int index = 0; index < 64; ++index)
    {
        const auto value = static_cast<std::byte>(index);
        auto frame = MakeSnapshot(pool, 4096, value);
        contentsPreserved = frame->pixels.front() == value && frame->pixels.back() == value && contentsPreserved;
    }
    releaser.join();
    return Expect(contentsPreserved && pool.GetRetention().entries <= 8 &&
        pool.GetRetention().bytes <= 32u * 1024u * 1024u,
        "concurrent acquisition and final release must preserve payloads and idle retention bounds");
}

bool RetentionBudgetsAndSizeShrink()
{
    DynamicTextureSnapshotPool entryLimited({ 2, 16384 });
    std::array<std::shared_ptr<Assets::TextureData>, 3> snapshots;
    for (auto& snapshot : snapshots) snapshot = MakeSnapshot(entryLimited, 1024);
    for (auto& snapshot : snapshots) snapshot.reset();
    bool passed = Expect(entryLimited.GetRetention().entries == 2 && entryLimited.GetRetention().bytes <= 16384,
        "released snapshots beyond the idle entry budget must be deleted");

    DynamicTextureSnapshotPool byteLimited({ 8, 2048 });
    for (auto& snapshot : snapshots) snapshot = MakeSnapshot(byteLimited, 1024);
    for (auto& snapshot : snapshots) snapshot.reset();
    passed &= Expect(byteLimited.GetRetention().bytes <= 2048 && byteLimited.GetRetention().entries == 2,
        "idle retention must count vector capacity and enforce its byte budget");
    auto oversized = MakeSnapshot(byteLimited, 8192);
    oversized.reset();
    passed &= Expect(byteLimited.GetRetention().bytes <= 2048,
        "an individual oversized snapshot must not bypass the byte budget");

    DynamicTextureSnapshotPool shrinking;
    auto large = MakeSnapshot(shrinking, 8192);
    large.reset();
    auto small = shrinking.Acquire(64);
    passed &= Expect(shrinking.GetRetention().entries == 0 && small->pixels.capacity() < 256,
        "small requests must discard idle capacities at least four times their size");
    return passed;
}

bool ImmutableMakerAndInvalidInputs()
{
    UI::UIContext ui(nullptr, nullptr);
    const std::vector<std::byte> pixels(4, std::byte{ 85 });
    auto first = ui.MakeDynamicTexture(1, 1, pixels);
    const auto* originalAddress = first.get();
    const auto originalId = first->id;
    std::weak_ptr<const Assets::TextureData> oldOwner = first;
    first.reset();
    const auto reused = ui.MakeDynamicTexture(1, 1, pixels);
    if (!Expect(reused.get() == originalAddress && reused->id != originalId && reused->revision == 0 &&
        oldOwner.expired() && !oldOwner.lock(),
        "MakeDynamicTexture must reuse exclusive storage while issuing a fresh immutable identity")) return false;
    std::shared_ptr<Assets::TextureData> current;
    ui.UpdateDynamicTexture(current, 1, 1, pixels);
    const auto original = current;
    ui.UpdateDynamicTexture(current, 0, 1, pixels);
    ui.UpdateDynamicTexture(current, 1, 1, {});
    ui.UpdateDynamicTexture(current, 0x80000000u, 0x80000000u, {});
    return Expect(current == original && !ui.MakeDynamicTexture(0, 1, pixels) &&
        !ui.MakeDynamicTexture(1, 1, {}) && !ui.MakeDynamicTexture(0x80000000u, 0x80000000u, {}),
        "invalid or overflowing dimensions must preserve the current texture and reject immutable creation");
}

bool PublishedFrameOutlivesContext()
{
    std::weak_ptr<Assets::TextureData> oldOwner;
    Rendering::RenderFrame retainedFrame;
    const std::vector<std::byte> red{
        std::byte{ 255 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 255 } };
    {
        UI::UIContext ui(nullptr, nullptr);
        std::shared_ptr<Assets::TextureData> texture;
        ui.UpdateDynamicTexture(texture, 1, 1, red);
        oldOwner = texture;
        Runtime::Input input;
        ui.BeginFrame(input, { 64, 64 });
        ui.DrawImage({ 0.0f, 0.0f, 64.0f, 64.0f }, texture);
        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ 64, 64 });
        ui.EndFrame(builder);
        retainedFrame = std::move(builder).Build();
        const auto published = texture;
        const Assets::TextureData* firstUpdate = nullptr;
        bool recycledExclusiveUpdate = false;
        for (int index = 0; index < 32; ++index)
        {
            ui.UpdateDynamicTexture(texture, 1, 1,
                { std::byte{ 0 }, std::byte{ 0 }, std::byte{ 255 }, std::byte{ 255 } });
            if (index == 0) firstUpdate = texture.get();
            if (index == 2) recycledExclusiveUpdate = texture.get() == firstUpdate;
        }
        if (!Expect(recycledExclusiveUpdate && published->pixels == red && published->revision == 1 &&
            texture->id == published->id && texture->revision == 33,
            "repeated UI updates must not recycle a snapshot retained by an older frame")) return false;
    }
    const auto& packet = retainedFrame.GetDrawPackets(Rendering::RenderPass::Transparent).at(0);
    const auto& draw = std::get<Rendering::SpriteDraw>(packet.payload);
    if (!Expect(retainedFrame.Validate().IsValid() &&
        retainedFrame.GetMaterial(draw.material)->baseColorTexture->pixels == red && !oldOwner.expired(),
        "a frame must retain its pixels after UIContext and its pool are destroyed")) return false;
    std::thread renderThread([frame = std::move(retainedFrame)]() mutable
    {
        frame = {};
    });
    renderThread.join();
    return Expect(oldOwner.expired(), "final release after context destruction must safely delete the snapshot");
}
}

bool RunDynamicTextureSnapshotPoolTests()
{
    return ReuseRequiresFinalSharedRelease() && CrossThreadFinalRelease() &&
        RetentionBudgetsAndSizeShrink() && ImmutableMakerAndInvalidInputs() && PublishedFrameOutlivesContext();
}

static const TestSupport::Registration gDynamicTextureSnapshotPoolTests{
    "UIContext", "dynamic texture storage should recycle only after final shared release", RunDynamicTextureSnapshotPoolTests };
