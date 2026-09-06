#include "D3D12UploadPoolTests.h"

#include <memory>

#include "Rendering/D3D12/D3D12UploadBufferPool.h"
#include "TestSupport.h"

namespace
{
    using Pool = GameEngine::Rendering::D3D12::D3D12UploadBufferPool<std::shared_ptr<int>, 3>;
    using TestSupport::Expect;

    [[nodiscard]] bool TestSlotCompletionAndDistinctCopies()
    {
        Pool pool;
        pool.BeginFrame(0);
        bool passed = Expect(pool.Commit({ std::make_shared<int>(1), 64 }),
            "the first upload should acquire an in-flight owner");
        passed &= Expect(pool.Commit({ std::make_shared<int>(2), 64 }),
            "a second copy should acquire a separate owner");
        passed &= Expect(!pool.Take(64).resource,
            "two copies in one recording must not overwrite an earlier copy's bytes");
        pool.BeginFrame(1);
        passed &= Expect(!pool.Take(64).resource,
            "another recording slot must not borrow an unfinished upload");
        passed &= Expect(pool.Commit({ std::make_shared<int>(3), 64 }),
            "another slot should own its upload independently");

        pool.BeginFrame(0);
        const Pool::Buffer first = pool.Take(64);
        const Pool::Buffer second = pool.Take(64);
        passed &= Expect(first.resource && second.resource && first.resource != second.resource,
            "a completed slot should recover two distinct upload resources");
        if (!first.resource || !second.resource)
        {
            return false;
        }
        passed &= Expect(*first.resource != 3 && *second.resource != 3,
            "completion of one slot must not reuse a later slot's pending upload");
        passed &= Expect(pool.Commit(first), "a borrowed upload should be republished before use");
        passed &= Expect(!pool.Take(64).resource,
            "republishing a buffer must prevent a second use in the same recording");
        passed &= Expect(pool.Commit(second), "the second borrowed upload should be republished");
        pool.BeginFrame(1);
        const Pool::Buffer third = pool.Take(64);
        passed &= Expect(third.resource && *third.resource == 3,
            "the other slot should retain its own bytes until its completion is known");
        return passed;
    }

    [[nodiscard]] bool TestBudgetAndInactiveSubmission()
    {
        Pool pool({ 128, 2, 120 });
        pool.BeginFrame(0);
        auto retained = std::make_shared<int>(1);
        auto oversized = std::make_shared<int>(2);
        const std::weak_ptr<int> retainedLifetime = retained;
        const std::weak_ptr<int> oversizedLifetime = oversized;
        bool passed = Expect(pool.Commit({ retained, 64 }), "a small upload should fit the budget");
        passed &= Expect(pool.Commit({ oversized, 256 }),
            "an upload exceeding the retention budget should still be accepted");
        retained.reset();
        oversized.reset();
        pool.BeginFrame(1);
        passed &= Expect(pool.Commit({ std::make_shared<int>(3), 64 }),
            "the retention budget should be shared by recording slots");
        pool.BeginFrame(2);
        passed &= Expect(pool.Commit({ std::make_shared<int>(4), 64 }),
            "retention pressure must not reject required GPU work");
        passed &= Expect(pool.GetRetainedByteSize() == 128 && pool.GetRetainedCount() == 2,
            "retained resources must stay inside both global limits");
        passed &= Expect(pool.GetInFlightCount() == 4 && !oversizedLifetime.expired(),
            "required in-flight resources may exceed the retention budget and must remain alive");

        pool.BeginFrame(1);
        passed &= Expect(oversizedLifetime.expired() && !retainedLifetime.expired(),
            "a later completed submission should retire an inactive slot's one-shot resources");
        passed &= Expect(pool.GetInFlightCount() == 1,
            "retirement must keep only submissions later than the known completed submission");
        passed &= Expect(pool.Commit({ std::make_shared<int>(5), 64 }),
            "new demand should evict completed cached storage when the budget is full");
        passed &= Expect(retainedLifetime.expired() && pool.GetRetainedByteSize() == 128,
            "eviction should release the oldest completed resource without exceeding the budget");

        Pool countLimited({ 1024, 1, 120 });
        countLimited.BeginFrame(0);
        passed &= Expect(countLimited.Commit({ std::make_shared<int>(1), 64 }) &&
            countLimited.Commit({ std::make_shared<int>(2), 64 }),
            "the entry limit should only bound retention");
        passed &= Expect(countLimited.GetRetainedCount() == 1 &&
            countLimited.GetInFlightCount() == 2,
            "the entry limit must not discard an unfinished upload");

        Pool uncached({ 0, 0, 120 });
        uncached.BeginFrame(0);
        auto uncachedResource = std::make_shared<int>(1);
        const std::weak_ptr<int> uncachedLifetime = uncachedResource;
        passed &= Expect(uncached.Commit({ uncachedResource, 64 }),
            "zero retention should support one-shot uploads");
        uncachedResource.reset();
        passed &= Expect(!uncachedLifetime.expired() && uncached.GetRetainedCount() == 0,
            "zero retention must preserve the GPU owner");
        uncached.BeginFrame(0);
        passed &= Expect(uncachedLifetime.expired(),
            "zero-retention resources should be released at completion");
        return passed;
    }

    [[nodiscard]] bool TestIdleFramesAndPendingOwners()
    {
        Pool pool({ 512, 8, 3 });
        pool.BeginFrame(0);
        auto resource = std::make_shared<int>(1);
        const std::weak_ptr<int> lifetime = resource;
        bool passed = Expect(pool.Commit({ resource, 64 }), "an idle candidate should be retained");
        resource.reset();
        for (int capture = 0; capture < 10; ++capture)
        {
            pool.BeginFrame(1, false);
        }
        passed &= Expect(!lifetime.expired() && pool.GetRetainedCount() == 1,
            "extra captures must not advance the cache's frame age");
        pool.BeginFrame(1);
        pool.BeginFrame(1);
        passed &= Expect(!lifetime.expired(), "retention should honor its frame grace");
        pool.BeginFrame(1);
        passed &= Expect(lifetime.expired() && pool.GetRetainedCount() == 0,
            "an inactive channel's completed cached uploads should expire with engine frames");

        Pool pending({ 512, 8, 1 });
        pending.BeginFrame(0);
        auto pendingResource = std::make_shared<int>(2);
        const std::weak_ptr<int> pendingLifetime = pendingResource;
        passed &= Expect(pending.Commit({ pendingResource, 64 }), "a pending upload should be owned");
        pendingResource.reset();
        pending.BeginFrame(1);
        passed &= Expect(pending.GetRetainedCount() == 0 && !pendingLifetime.expired(),
            "age eviction must not release a resource before any submission is known complete");
        pending.BeginFrame(1);
        passed &= Expect(pendingLifetime.expired(),
            "the last GPU owner should retire when a later submission completes");
        return passed;
    }

    [[nodiscard]] bool TestChangingDemandAndAbandonedBorrow()
    {
        Pool pool({ 512, 8, 120 });
        pool.BeginFrame(0);
        auto first = std::make_shared<int>(1);
        auto second = std::make_shared<int>(2);
        const std::weak_ptr<int> firstLifetime = first;
        const std::weak_ptr<int> secondLifetime = second;
        bool passed = Expect(pool.Commit({ first, 64 }) && pool.Commit({ second, 64 }),
            "a slot should retain its current upload demand");
        first.reset();
        second.reset();
        pool.BeginFrame(0);
        Pool::Buffer used = pool.Take(64);
        if (!Expect(static_cast<bool>(used.resource), "the completed demand should be reusable"))
        {
            return false;
        }
        const int usedValue = *used.resource;
        passed &= Expect(pool.Commit(used), "the reduced demand should be published");
        used = {};
        pool.BeginFrame(0);
        passed &= Expect(pool.GetRetainedCount() == 1 &&
            (usedValue == 1 ? secondLifetime.expired() : firstLifetime.expired()),
            "buffers unused by the slot's preceding recording should be released");

        Pool sizeChanges({ 1024, 8, 120 });
        sizeChanges.BeginFrame(0);
        auto large = std::make_shared<int>(3);
        const std::weak_ptr<int> largeLifetime = large;
        passed &= Expect(sizeChanges.Commit({ large, 256 }), "a large upload should be retained");
        large.reset();
        sizeChanges.BeginFrame(0);
        passed &= Expect(!sizeChanges.Take(32).resource && largeLifetime.expired(),
            "a substantially smaller upload should retire excessive completed capacity");
        passed &= Expect(sizeChanges.Commit({ std::make_shared<int>(4), 32 }),
            "smaller storage should replace excessive capacity");
        sizeChanges.BeginFrame(0);
        passed &= Expect(!sizeChanges.Take(128).resource,
            "growing uploads must not reuse an undersized resource");
        passed &= Expect(sizeChanges.Commit({ std::make_shared<int>(5), 128 }),
            "a growing workload should retain its replacement storage");
        sizeChanges.BeginFrame(0);
        passed &= Expect(sizeChanges.GetRetainedByteSize() == 128,
            "unused undersized storage should retire after the replacement recording completes");
        Pool::Buffer abandoned = sizeChanges.Take(128);
        const std::weak_ptr<int> abandonedLifetime = abandoned.resource;
        passed &= Expect(abandoned.resource && sizeChanges.GetInFlightCount() == 0,
            "a borrowed resource must not be published before mapping succeeds");
        abandoned = {};
        passed &= Expect(abandonedLifetime.expired() && sizeChanges.GetRetainedCount() == 0,
            "abandoning a borrow should release it without adding pending GPU ownership");

        Pool inactive;
        passed &= Expect(!inactive.Commit({ std::make_shared<int>(1), 64 }) &&
            inactive.GetInFlightCount() == 0,
            "uploads must have an active recording slot before ownership is published");
        return passed;
    }
}

bool RunD3D12UploadPoolTests()
{
    bool passed = TestSlotCompletionAndDistinctCopies();
    passed &= TestBudgetAndInactiveSubmission();
    passed &= TestIdleFramesAndPendingOwners();
    passed &= TestChangingDemandAndAbandonedBorrow();
    return passed;
}

static const TestSupport::Registration gD3D12UploadPoolTests{
    "RenderCache", "D3D12 upload reuse should respect GPU completion and bounded retention",
    RunD3D12UploadPoolTests };
