#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "FrameBoundCache.h"
#include "RenderResourceCachePolicy.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 한 종류의 백엔드 리소스를 예산과 in-flight 프레임 수가 허락하는 만큼 보관한다.
///
/// 백엔드 리졸버가 리소스 종류마다 다시 쓰지 않는 순서가 여기 한 번 있다: 캐시를 찾아보고,
/// 이 키가 이미 실패했는지 확인하고, 이번 프레임에 공간 부족으로 거절됐는지 확인하고, 만들고,
/// 자리를 비우고, 삽입하고, 예산에 닿으면 프레임당 한 번 경고한다. 백엔드마다 복사하면 복사본
/// 하나하나가 퇴거·재시도 규칙이 서로 어긋나는 자리가 된다 — 퇴거가 일어날 만큼 큰 장면에서만
/// 드러나는 종류의 어긋남이다.
///
/// 백엔드가 여전히 직접 쓰는 것은 build 단계뿐이며, 그것이 진짜로 자기 API에 관한 부분이다.
///
/// 자리는 리소스를 만들기 전에 비운다. 반대 순서가 솔깃한 이유는 실패한 build가 퇴거 비용을
/// 치르지 않아서지만, 그것은 흔한 경우를 거꾸로 잡은 것이다: 캐시가 가득 차면 그 프레임의 남은
/// 리소스를 전부 돌려보내고 다음 프레임에 재시도하므로, 먼저 만들면 그 모든 프레임마다 버려질
/// GPU 업로드 값을 치르게 된다. 반면 build 실패는 기록되어 다시 시도되지 않으므로, 낭비된 퇴거
/// 한 번이 비용의 전부다.
/// </summary>
template <typename TResource>
class ResolvedResourceCache final
{
public:
    /// <param name="budget">이 리소스 종류의 항목 수와 바이트 한도이다.</param>
    /// <param name="name">
    /// 용량 경고에서 이 캐시를 부를 이름이다. 예: "D3D11 mesh". 리졸버 전체가 하나의 스로틀을
    /// 공유하는 대신 캐시마다 스스로 경고하므로, 가득 찬 로그는 "그중 하나가 찼다"가 아니라
    /// 어느 예산이 닿았는지를 말해 준다.
    /// </param>
    /// <param name="retainedFrames">
    /// 몇 프레임 치 항목을 퇴거로부터 보호할지이다. CPU만 읽는 캐시에는 1이 맞고, GPU 리소스를
    /// 쥔 캐시는 백엔드가 in flight로 유지하는 프레임 수가 필요하다. 그렇지 않으면 퇴거가 이미
    /// 기록된 command list가 아직 그리고 있는 것을 해제할 수 있다.
    /// </param>
    ResolvedResourceCache(
        const RenderResourceCacheBudget& budget,
        const std::string_view name,
        const std::uint64_t retainedFrames = 1)
        : mEntries(budget.maximumEntries, budget.maximumBytes, retainedFrames)
        , mFailed(budget)
        , mCapacityRejected(budget)
        , mCapacityMessage(std::string(name) + " cache reached its entry or memory limit.")
    {
    }

    ResolvedResourceCache(const ResolvedResourceCache&) = delete;
    ResolvedResourceCache& operator=(const ResolvedResourceCache&) = delete;

    /// <summary>
    /// 사용 수명을 진행시키고 이번 프레임의 거절 기록을 다시 연다.
    ///
    /// 용량 거절은 한 프레임의 경합을 설명할 뿐이므로 재시도해야 하고, build 실패는 기록으로
    /// 남아 깨진 에셋을 매 프레임 다시 만들지 않게 한다. `BoundedKeySet`이 그 기록에 상한을
    /// 두어, 기록이 자기가 지키는 캐시보다 커질 수 없게 한다.
    /// </summary>
    void BeginFrame()
    {
        mEntries.BeginFrame();
        mCapacityRejected.Clear();
        mCapacityWarning.BeginFrame();
    }

    /// <summary>
    /// 이 키의 리소스를 반환하고, 아직 캐시에 없으면 만든다.
    ///
    /// `byteSize`는 이 항목이 차지할 비용이며, 호출자는 무엇을 만들기도 전에 프레임 데이터에서
    /// 이를 안다. `build`는 채울 빈 리소스를 받아 성공 여부를 반환한다. false를 반환하면 키가
    /// 기록되어 같은 깨진 리소스를 다시 시도하지 않는다.
    ///
    /// 리소스를 만들 수 없거나 들어갈 자리가 없으면 null을 반환한다. 호출자는 그 draw를
    /// 버린다: 여기서 반환된 포인터는 보존 프레임 동안 유효하므로, 포인터를 받아 간 draw가
    /// 같은 프레임의 나중 resolve 때문에 무효화되는 일은 없다.
    /// </summary>
    /// <summary>
    /// 캐시된 항목을 고칠 수 있게 돌려준다. 같은 id의 픽셀이 바뀐 텍스처를 제자리에서 갱신하는
    /// 경로가 쓴다. 없으면 null이다. 찾는 것은 사용으로 기록된다.
    /// </summary>
    [[nodiscard]] TResource* FindMutable(const std::uint64_t key) { return mEntries.Find(key); }

    template <typename TBuild>
    [[nodiscard]] const TResource* Resolve(
        const std::uint64_t key, const std::size_t byteSize, TBuild&& build)
    {
        if (const TResource* const cached = mEntries.Find(key))
        {
            return cached;
        }
        if (mFailed.Contains(key) || mCapacityRejected.Contains(key))
        {
            return nullptr;
        }

        if (!mEntries.MakeRoom(byteSize))
        {
            mCapacityRejected.Record(key);
            mCapacityWarning.Warn(mCapacityMessage.c_str());
            return nullptr;
        }

        TResource resource;
        if (!std::forward<TBuild>(build)(resource))
        {
            mFailed.Record(key);
            return nullptr;
        }
        return mEntries.Insert(key, std::move(resource), byteSize);
    }

    [[nodiscard]] std::size_t GetSize() const { return mEntries.GetSize(); }
    [[nodiscard]] std::size_t GetByteSize() const { return mEntries.GetByteSize(); }

private:
    FrameBoundCache<std::uint64_t, TResource> mEntries;
    BoundedKeySet<std::uint64_t> mFailed;
    BoundedKeySet<std::uint64_t> mCapacityRejected;
    CapacityWarningThrottle mCapacityWarning;
    std::string mCapacityMessage;
};

}
