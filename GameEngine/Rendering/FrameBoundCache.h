#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>

namespace GameEngine::Rendering
{

/// <summary>
/// 바이트 예산으로 제한되며, 현재 프레임에 반환된 항목은 퇴거될 수 없는 캐시이다.
/// BeginFrame이 렌더 리소스 리졸버가 사용하는 포인터 사용 수명을 정의한다.
/// </summary>
template <typename TKey, typename TValue, typename THash = std::hash<TKey>>
class FrameBoundCache final
{
public:
    struct Entry
    {
        TValue value;
        std::size_t byteSize = 0;
        std::uint64_t lastUsedFrame = 0;
    };

    /// <summary>
    /// 캐시를 제한하고, 최근 몇 프레임의 항목을 퇴거로부터 보호할지 정한다.
    ///
    /// CPU만 읽는 캐시에는 한 프레임이 맞다: 프레임이 만들어지고 나면 지난 프레임의 항목은
    /// 자유롭다. GPU 리소스를 쥔 캐시는 렌더러가 in flight로 유지하는 프레임 수만큼 필요하다.
    /// 두 프레임 전에 기록된 command list가 아직 실행 중일 수 있고, 그것이 참조하는 항목을
    /// 퇴거하면 GPU가 읽고 있는 리소스를 해제하는 셈이 되기 때문이다.
    /// </summary>
    /// <param name="idleFrames">
    /// 아무것도 찾지 않는 항목을 얼마나 오래 간직할 가치가 있는지이다.
    ///
    /// `retainedFrames`와는 다른 질문이며 둘을 혼동해서는 안 된다: 그쪽은 무언가 아직 읽고 있을
    /// 수 있어 항목을 퇴거하면 *안 되는* 기간이고, 이쪽은 아무도 원하지 않게 된 항목을 간직할
    /// *가치가 있는* 기간이다. 메모리 압박이 없는 작은 장면에서도 이 기간이 지나면 사용하지
    /// 않는 리소스를 퇴거한다. resolve된 리소스가 쥐는 에셋 참조도 함께 놓인다.
    /// </param>
    FrameBoundCache(
        const std::size_t maximumEntries,
        const std::size_t maximumBytes,
        const std::uint64_t retainedFrames = 1,
        const std::uint64_t idleFrames = DefaultIdleFrames)
        : mMaximumEntries(maximumEntries),
          mMaximumBytes(maximumBytes),
          mRetainedFrames(retainedFrames == 0 ? 1 : retainedFrames),
          mIdleFrames(idleFrames)
    {
    }

    /// <summary>
    /// 초당 60프레임 기준 약 5초이다. 무언가에서 눈을 돌렸다 다시 봐도 다시 로드하지 않을 만큼
    /// 길고, 한 장면 분량의 리소스가 사람이 알아챌 만큼 장면보다 오래 살지는 않을 만큼 짧다.
    /// </summary>
    static constexpr std::uint64_t DefaultIdleFrames = 300;

    /// <summary>
    /// 항목 수 상한을 다시 정한다. 상한이 무엇인지가 생성 시점에는 아직 알려지지 않고 장치를
    /// 열어봐야 아는 캐시를 위한 것이다 — D3D12의 바인딩 캐시는 descriptor 힙이 실제로 몇 자리를
    /// 얻었는지에 묶여 있고, 그 수는 정책과 장치 한계를 결합해서야 나온다.
    ///
    /// 이미 담긴 항목보다 낮게 정해도 안전하다. 여기서 아무것도 버리지 않고, 다음 MakeRoom이
    /// 평소의 퇴거 규칙대로 — 보존 창 안의 항목은 건드리지 않고 — 줄여 나간다.
    /// </summary>
    void SetMaximumEntries(const std::size_t maximumEntries)
    {
        mMaximumEntries = maximumEntries;
    }

    void BeginFrame()
    {
        ++mFrameIndex;
        if (mFrameIndex == 0)
        {
            mFrameIndex = 1;
            for (auto& [key, entry] : mEntries)
            {
                entry.lastUsedFrame = 0;
            }
        }
        RetireIdleEntries();
    }

    /// <summary>
    /// 오랫동안 아무것도 찾지 않은 것을 버린다. 보존 창 밖의 항목만 대상으로 하므로, 무언가
    /// 아직 읽고 있을 수 있는 항목을 가져가는 일은 결코 없다.
    /// </summary>
    void RetireIdleEntries()
    {
        if (mIdleFrames == 0 || mEntries.empty())
        {
            return;
        }
        const std::uint64_t horizon = mRetainedFrames + mIdleFrames;
        if (mFrameIndex <= horizon)
        {
            return;
        }
        const std::uint64_t oldestKept = mFrameIndex - horizon;
        std::erase_if(
            mEntries,
            [this, oldestKept](const auto& entry)
            {
                if (entry.second.lastUsedFrame > oldestKept)
                {
                    return false;
                }
                mBytes -= entry.second.byteSize;
                return true;
            });
    }

    [[nodiscard]] TValue* Find(const TKey& key)
    {
        const auto entry = mEntries.find(key);
        if (entry == mEntries.end())
        {
            return nullptr;
        }
        entry->second.lastUsedFrame = mFrameIndex;
        return &entry->second.value;
    }

    /// <summary>이번 프레임에 얻어 간 결과를 무효화하지 않으면서 공간을 확보한다.</summary>
    [[nodiscard]] bool MakeRoom(const std::size_t newEntryBytes)
    {
        if (newEntryBytes > mMaximumBytes)
        {
            return false;
        }

        while (mEntries.size() >= mMaximumEntries || mBytes > mMaximumBytes - newEntryBytes)
        {
            auto oldestUnused = mEntries.end();
            for (auto entry = mEntries.begin(); entry != mEntries.end(); ++entry)
            {
                // Protected while it is still within the retained window: for a GPU cache that
                // window is the frames in flight, so an entry an executing command list references
                // is never released.
                if (entry->second.lastUsedFrame + mRetainedFrames > mFrameIndex)
                {
                    continue;
                }
                if (oldestUnused == mEntries.end() ||
                    entry->second.lastUsedFrame < oldestUnused->second.lastUsedFrame)
                {
                    oldestUnused = entry;
                }
            }
            if (oldestUnused == mEntries.end())
            {
                return false;
            }
            mBytes -= oldestUnused->second.byteSize;
            mEntries.erase(oldestUnused);
        }
        return true;
    }

    [[nodiscard]] TValue* Insert(TKey key, TValue value, const std::size_t byteSize)
    {
        const auto [entry, inserted] = mEntries.emplace(
            std::move(key),
            Entry{ std::move(value), byteSize, mFrameIndex });
        if (!inserted)
        {
            return nullptr;
        }
        mBytes += byteSize;
        return &entry->second.value;
    }

    [[nodiscard]] std::size_t GetSize() const { return mEntries.size(); }
    [[nodiscard]] std::size_t GetByteSize() const { return mBytes; }
    [[nodiscard]] std::uint64_t GetFrameIndex() const { return mFrameIndex; }

private:
    std::size_t mMaximumEntries;
    std::size_t mMaximumBytes;
    std::uint64_t mRetainedFrames = 1;
    std::uint64_t mIdleFrames = DefaultIdleFrames;
    std::unordered_map<TKey, Entry, THash> mEntries;
    std::size_t mBytes = 0;
    std::uint64_t mFrameIndex = 0;
};

}
