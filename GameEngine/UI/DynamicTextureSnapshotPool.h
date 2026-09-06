#pragma once

#include <cstddef>
#include <memory>

#include "../Assets/TextureData.h"

namespace GameEngine::UI::Detail
{

struct DynamicTextureSnapshotBudget
{
    std::size_t maximumEntries = 8;
    std::size_t maximumBytes = 32u * 1024u * 1024u;
};

struct DynamicTextureSnapshotRetention
{
    std::size_t entries = 0;
    std::size_t bytes = 0;
};

/// <summary>
/// Only the last shared owner's deleter returns a snapshot to this pool. Acquire transfers an
/// exclusively owned object and Publish creates a fresh control block, so old weak owners cannot
/// observe a recycled snapshot. The retention budget counts idle capacity, excluding live frames.
/// </summary>
class DynamicTextureSnapshotPool final
{
public:
    explicit DynamicTextureSnapshotPool(const DynamicTextureSnapshotBudget budget = {});
    ~DynamicTextureSnapshotPool();
    DynamicTextureSnapshotPool(const DynamicTextureSnapshotPool&) = delete;
    DynamicTextureSnapshotPool& operator=(const DynamicTextureSnapshotPool&) = delete;

    /// <summary>
    /// Returns an empty writable payload with at least the requested capacity; zero returns null.
    /// Idle buffers at least four times the requested size are discarded to allow size shrinkage.
    /// </summary>
    [[nodiscard]] std::unique_ptr<Assets::TextureData> Acquire(std::size_t requiredBytes);

    /// <summary>
    /// Publishes a completed payload. Final release is safe on another thread or after this pool
    /// is destroyed; a weak pool reference keeps published frames from retaining idle memory.
    /// </summary>
    [[nodiscard]] std::shared_ptr<Assets::TextureData> Publish(std::unique_ptr<Assets::TextureData> texture);

    [[nodiscard]] DynamicTextureSnapshotRetention GetRetention() const;

private:
    struct State;
    std::shared_ptr<State> mState;
};

}
