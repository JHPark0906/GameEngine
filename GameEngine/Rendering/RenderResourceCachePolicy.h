#pragma once

#include <cstddef>
#include <functional>
#include <unordered_set>

#include "../Diagnostics/Debug.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 리졸버가 한 종류의 리소스를 얼마나 상주시켜도 되는지이다. 예산이 백엔드가 아니라 여기에 있는
/// 이유는, 디코딩된 메시·텍스처·텍스트 비트맵이 캐시에 얼마나 머무는지가 그래픽 API의 속성이
/// 아니라 렌더링 정책이기 때문이다. 예산이 백엔드마다 다르면 같은 장면이 서로 다른 만큼
/// 상주하며 서로 다른 퇴거 경로를 밟고, 재로드 경로의 버그가 마침 퇴거가 일어난 백엔드에서만
/// 재현된다.
/// </summary>
struct RenderResourceCacheBudget
{
    std::size_t maximumEntries = 0;
    std::size_t maximumBytes = 0;
};

/// <summary>
/// 모든 백엔드의 리소스 리졸버가 쓰는 예산이다. 임포트된 메시 하나가 텍스처보다 무거우므로
/// 메시가 더 큰 바이트 예산을 받고, 텍스처와 래스터화된 텍스트는 더 작은 예산을 나눠 쓴다.
/// </summary>
inline constexpr RenderResourceCacheBudget MeshCacheBudget{ 256, 256ull * 1024ull * 1024ull };
inline constexpr RenderResourceCacheBudget TextureCacheBudget{ 256, 128ull * 1024ull * 1024ull };
inline constexpr RenderResourceCacheBudget TextCacheBudget{ 256, 128ull * 1024ull * 1024ull };

/// <summary>
/// GPU에 상주하는 텍스처 바인딩의 예산이다. D3D12의 업로더가 지키는 캐시가 이것을 읽는다.
///
/// 위의 예산들과 이름을 나눈 이유는, 같은 이름이 백엔드마다 다른 것을 뜻하기 때문이다.
/// 리졸버가 무엇을 쥐는지가 백엔드마다 다르다: D3D11의 resolved 텍스처는 곧 GPU 텍스처라서
/// <see cref="TextureCacheBudget"/>과 <see cref="TextCacheBudget"/>이 이미 GPU 바이트를 센다.
/// D3D12의 resolved 텍스처는 업로드할 픽셀만 쥐고 GPU 텍스처는 업로더의 바인딩 캐시에 따로
/// 살아서, 같은 두 예산이 거기서는 CPU 바이트를 센다. 그래서 GPU 상주를 뜻하는 예산에는 다른
/// 이름을 준다 — 한 이름이 한쪽에서 GPU 바이트, 다른 쪽에서 CPU 바이트를 뜻하면 그것은 통일이
/// 아니라 혼란이다.
///
/// 값을 두 예산의 합으로 두는 것이 그 통일이다. D3D11은 텍스처와 텍스트를 각각의 캐시에 GPU로
/// 상주시키고 D3D12는 그 둘을 바인딩 캐시 하나에 함께 상주시키므로, 합이라야 같은 장면이 두
/// 백엔드에서 같은 만큼 상주하고 같은 지점에서 함께 퇴거한다.
/// </summary>
inline constexpr RenderResourceCacheBudget TextureBindingBudget{
    TextureCacheBudget.maximumEntries + TextCacheBudget.maximumEntries,
    TextureCacheBudget.maximumBytes + TextCacheBudget.maximumBytes };

/// <summary>
/// 리졸버가 다시 시도하지 않기로 한 키들이다. 자기가 지키는 캐시의 예산으로 크기가 제한된다.
///
/// 실패를 기억하면 깨진 에셋을 매 프레임 다시 로드하는 일이 없어지지만, 영원히 기억하면 이
/// 집합이 자기가 지키는 캐시보다 커질 수 있다. 상한에 닿으면 새 키를 거절하는 대신 집합을
/// 비우므로, 억제되는 것은 언제나 가장 최근의 실패들이다.
/// </summary>
template <typename TKey, typename THash = std::hash<TKey>>
class BoundedKeySet final
{
public:
    explicit BoundedKeySet(const RenderResourceCacheBudget& budget)
        : mMaximumEntries(budget.maximumEntries)
    {
    }

    [[nodiscard]] bool Contains(const TKey& key) const { return mKeys.contains(key); }

    void Record(const TKey& key)
    {
        if (mKeys.size() >= mMaximumEntries)
        {
            mKeys.clear();
        }
        mKeys.insert(key);
    }

    void Clear() { mKeys.clear(); }

    [[nodiscard]] std::size_t GetSize() const { return mKeys.size(); }

private:
    std::size_t mMaximumEntries;
    std::unordered_set<TKey, THash> mKeys;
};

/// <summary>
/// 캐시 압박을 프레임당 한 번만 보고한다. 가득 찬 캐시는 그 프레임의 남은 리소스를 전부
/// 거절하므로, 거절마다 경고하면 로그가 똑같은 줄에 파묻힌다.
/// </summary>
class CapacityWarningThrottle final
{
public:
    void BeginFrame() { mWarnedThisFrame = false; }

    void Warn(const char* const message)
    {
        if (!mWarnedThisFrame)
        {
            Diagnostics::Debug::LogWarning(message);
            mWarnedThisFrame = true;
        }
    }

private:
    bool mWarnedThisFrame = false;
};

}
