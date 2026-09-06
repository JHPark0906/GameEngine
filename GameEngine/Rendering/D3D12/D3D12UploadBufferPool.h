#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace GameEngine::Rendering::D3D12
{

struct D3D12UploadPoolBudget
{
    std::size_t maximumBytes = 128ull * 1024 * 1024;
    std::size_t maximumEntries = 512;
    std::uint64_t idleFrames = 120;
};

/// <summary>
/// Upload resources belong to one ordered queue. BeginFrame requires the selected slot's prior
/// work to be complete; completion of that submission also completes all earlier submissions.
/// Reuse is restricted to that slot and to completed work. The retention budget excludes resources
/// required by unfinished copies: those keep a separate owner until their submission completes.
/// </summary>
template <class TResource, std::size_t SlotCount>
class D3D12UploadBufferPool final
{
    static_assert(SlotCount > 0);

public:
    struct Buffer
    {
        TResource resource;
        std::size_t capacity = 0;
    };

    explicit D3D12UploadBufferPool(const D3D12UploadPoolBudget budget = {})
        : mBudget(budget)
    {
    }

    D3D12UploadBufferPool(const D3D12UploadBufferPool&) = delete;
    D3D12UploadBufferPool& operator=(const D3D12UploadBufferPool&) = delete;

    /// <summary>
    /// The caller completes the selected slot's fence before this call and submits recordings in
    /// BeginFrame order on one queue. A capture in the same engine frame passes advanceFrame=false.
    /// </summary>
    void BeginFrame(const std::size_t slotIndex, const bool advanceFrame = true)
    {
        mCurrentSlot = slotIndex % SlotCount;
        const std::uint64_t previousSerial = mSlots[mCurrentSlot].serial;
        if (previousSerial > mCompletedSerial)
        {
            mCompletedSerial = previousSerial;
        }
        for (Slot& slot : mSlots)
        {
            if (slot.serial <= mCompletedSerial)
            {
                slot.inFlight.clear();
            }
        }

        if (advanceFrame)
        {
            if (mFrame == (std::numeric_limits<std::uint64_t>::max)())
            {
                // Cache ownership can expire independently of the in-flight owners.
                mRetained.clear();
                mRetainedBytes = 0;
                mFrame = 0;
            }
            ++mFrame;
        }
        for (std::size_t index = 0; index < mRetained.size();)
        {
            const Entry& entry = mRetained[index];
            const bool unusedBySlot = entry.slot == mCurrentSlot &&
                entry.lastUsedSerial < previousSerial;
            const bool idle = mFrame - entry.lastUsedFrame >= mBudget.idleFrames;
            if (unusedBySlot || idle)
            {
                RemoveRetained(index);
            }
            else
            {
                ++index;
            }
        }

        if (mSerial == (std::numeric_limits<std::uint64_t>::max)())
        {
            // With finitely many slots, completion advances before the serial can be exhausted.
            const std::uint64_t offset = mCompletedSerial;
            for (Slot& slot : mSlots)
            {
                slot.serial = slot.serial > offset ? slot.serial - offset : 0;
            }
            for (Entry& entry : mRetained)
            {
                entry.lastUsedSerial = entry.lastUsedSerial > offset ?
                    entry.lastUsedSerial - offset : 0;
            }
            mSerial -= offset;
            mCompletedSerial = 0;
        }
        mSlots[mCurrentSlot].serial = ++mSerial;
    }

    /// <summary>
    /// Borrow the smallest completed buffer that fits. A buffer at least four times the requested
    /// capacity is retired so a smaller workload can release its large allocations. An abandoned
    /// borrow records no GPU ownership; Commit is required only after a successful map and copy.
    /// </summary>
    [[nodiscard]] Buffer Take(const std::size_t requiredCapacity)
    {
        if (requiredCapacity == 0)
        {
            return {};
        }
        for (std::size_t index = 0; index < mRetained.size();)
        {
            const Entry& entry = mRetained[index];
            if (entry.slot == mCurrentSlot && entry.lastUsedSerial <= mCompletedSerial &&
                entry.buffer.capacity / requiredCapacity >= 4)
            {
                RemoveRetained(index);
            }
            else
            {
                ++index;
            }
        }
        std::size_t best = mRetained.size();
        for (std::size_t index = 0; index < mRetained.size(); ++index)
        {
            const Entry& entry = mRetained[index];
            if (entry.slot == mCurrentSlot && entry.lastUsedSerial <= mCompletedSerial &&
                entry.buffer.capacity >= requiredCapacity &&
                (best == mRetained.size() ||
                    entry.buffer.capacity < mRetained[best].buffer.capacity))
            {
                best = index;
            }
        }
        if (best == mRetained.size())
        {
            return {};
        }
        Buffer result = std::move(mRetained[best].buffer);
        RemoveRetained(best);
        return result;
    }

    /// <summary>
    /// Publish ownership before recording a command that references the buffer. Exceeding the
    /// retention budget uses a one-shot owner and does not reject the upload. Allocation failures
    /// in this bookkeeping occur before the caller records any resource state changes or copies.
    /// </summary>
    [[nodiscard]] bool Commit(const Buffer& buffer)
    {
        if (mSerial == 0 || !buffer.resource || buffer.capacity == 0)
        {
            return false;
        }
        mSlots[mCurrentSlot].inFlight.push_back(buffer.resource);
        if (mBudget.maximumEntries == 0 || buffer.capacity > mBudget.maximumBytes)
        {
            return true;
        }
        while (mRetained.size() >= mBudget.maximumEntries ||
            mRetainedBytes > mBudget.maximumBytes - buffer.capacity)
        {
            std::size_t oldest = mRetained.size();
            for (std::size_t index = 0; index < mRetained.size(); ++index)
            {
                if (mRetained[index].lastUsedSerial <= mCompletedSerial &&
                    (oldest == mRetained.size() ||
                        mRetained[index].lastUsedSerial < mRetained[oldest].lastUsedSerial))
                {
                    oldest = index;
                }
            }
            if (oldest == mRetained.size())
            {
                return true;
            }
            RemoveRetained(oldest);
        }
        mRetained.push_back({ buffer, mCurrentSlot, mSerial, mFrame });
        mRetainedBytes += buffer.capacity;
        return true;
    }

    [[nodiscard]] std::size_t GetRetainedByteSize() const { return mRetainedBytes; }
    [[nodiscard]] std::size_t GetRetainedCount() const { return mRetained.size(); }
    [[nodiscard]] std::size_t GetInFlightCount() const
    {
        std::size_t count = 0;
        for (const Slot& slot : mSlots)
        {
            count += slot.inFlight.size();
        }
        return count;
    }

private:
    struct Entry
    {
        Buffer buffer;
        std::size_t slot = 0;
        std::uint64_t lastUsedSerial = 0;
        std::uint64_t lastUsedFrame = 0;
    };

    struct Slot
    {
        std::uint64_t serial = 0;
        std::vector<TResource> inFlight;
    };

    void RemoveRetained(const std::size_t index)
    {
        mRetainedBytes -= mRetained[index].buffer.capacity;
        if (index + 1 < mRetained.size())
        {
            mRetained[index] = std::move(mRetained.back());
        }
        mRetained.pop_back();
    }

    D3D12UploadPoolBudget mBudget;
    std::array<Slot, SlotCount> mSlots;
    std::vector<Entry> mRetained;
    std::size_t mRetainedBytes = 0;
    std::size_t mCurrentSlot = 0;
    std::uint64_t mSerial = 0;
    std::uint64_t mCompletedSerial = 0;
    std::uint64_t mFrame = 0;
};

}
