#include "pch.h"
#include "DynamicTextureSnapshotPool.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <system_error>
#include <utility>

namespace GameEngine::UI::Detail
{

struct DynamicTextureSnapshotPool::State
{
    static constexpr std::size_t MaximumEntries = 8;
    static constexpr std::size_t MaximumBytes = 32u * 1024u * 1024u;

    explicit State(const DynamicTextureSnapshotBudget requestedBudget)
        : budget{ (std::min)(requestedBudget.maximumEntries, MaximumEntries),
              (std::min)(requestedBudget.maximumBytes, MaximumBytes) }
    {
    }

    [[nodiscard]] std::unique_ptr<Assets::TextureData> Remove(const std::size_t index)
    {
        auto result = std::move(idle[index]);
        retainedBytes -= result->pixels.capacity();
        --count;
        if (index != count) idle[index] = std::move(idle[count]);
        return result;
    }

    void Return(std::unique_ptr<Assets::TextureData> texture) noexcept
    {
        const std::size_t capacity = texture->pixels.capacity();
        try
        {
            const std::lock_guard lock(mutex);
            if (capacity != 0 && count < budget.maximumEntries &&
                capacity <= budget.maximumBytes - retainedBytes)
            {
                retainedBytes += capacity;
                idle[count++] = std::move(texture);
            }
        }
        catch (const std::system_error&)
        {
            // A deleter cannot propagate a mutex failure; its local owner deletes the payload.
        }
    }

    std::mutex mutex;
    const DynamicTextureSnapshotBudget budget;
    std::array<std::unique_ptr<Assets::TextureData>, MaximumEntries> idle;
    std::size_t count = 0;
    std::size_t retainedBytes = 0;
};

DynamicTextureSnapshotPool::DynamicTextureSnapshotPool(const DynamicTextureSnapshotBudget budget)
    : mState(std::make_shared<State>(budget))
{
}

DynamicTextureSnapshotPool::~DynamicTextureSnapshotPool() = default;

std::unique_ptr<Assets::TextureData> DynamicTextureSnapshotPool::Acquire(const std::size_t requiredBytes)
{
    if (requiredBytes == 0) return nullptr;
    std::unique_ptr<Assets::TextureData> texture;
    std::array<std::unique_ptr<Assets::TextureData>, State::MaximumEntries> discarded;
    std::size_t discardCount = 0;
    {
        const std::lock_guard lock(mState->mutex);
        for (std::size_t index = 0; index < mState->count;)
        {
            if (mState->idle[index]->pixels.capacity() / requiredBytes >= 4)
            {
                discarded[discardCount++] = mState->Remove(index);
            }
            else
            {
                ++index;
            }
        }
        std::size_t best = mState->count;
        for (std::size_t index = 0; index < mState->count; ++index)
        {
            const auto capacity = mState->idle[index]->pixels.capacity();
            if (capacity >= requiredBytes &&
                (best == mState->count || capacity < mState->idle[best]->pixels.capacity()))
            {
                best = index;
            }
        }
        if (best != mState->count) texture = mState->Remove(best);
    }
    if (!texture) texture = std::make_unique<Assets::TextureData>();
    texture->id = 0;
    texture->revision = 0;
    texture->width = 0;
    texture->height = 0;
    texture->pixels.clear();
    texture->pixels.reserve(requiredBytes);
    return texture;
}

std::shared_ptr<Assets::TextureData> DynamicTextureSnapshotPool::Publish(
    std::unique_ptr<Assets::TextureData> texture)
{
    if (!texture) return {};
    const std::weak_ptr<State> pool = mState;
    return { texture.release(), [pool](Assets::TextureData* raw) noexcept
    {
        std::unique_ptr<Assets::TextureData> released(raw);
        if (const auto state = pool.lock()) state->Return(std::move(released));
    } };
}

DynamicTextureSnapshotRetention DynamicTextureSnapshotPool::GetRetention() const
{
    const std::lock_guard lock(mState->mutex);
    return { mState->count, mState->retainedBytes };
}

}
