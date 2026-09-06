#pragma once

#include <cstddef>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Asset.h"
#include "AudioData.h"
#include "MaterialData.h"
#include "MeshData.h"
#include "SkinnedMeshData.h"
#include "TextureData.h"
#include "../Animation/AnimationClip.h"
#include "../Animation/Skeleton.h"

namespace GameEngine::Platform
{
class IContentSource;
}

namespace GameEngine::Assets
{

/// <summary>
/// 한 파일이 내놓은 모든 것이다. 함께 보관하는 이유는 한 번의 파싱이 전부를 만들기
/// 때문이다: 모델은 첫 메시를 만들지 않고서는 두 번째 메시를 건네줄 수 없다.
/// </summary>
struct LoadedFile
{
    std::vector<std::shared_ptr<const MeshData>> meshes;
    std::vector<std::shared_ptr<const TextureData>> images;
    std::vector<std::shared_ptr<const AudioData>> audioClips;
    std::vector<std::shared_ptr<const SkinnedMeshData>> skinnedMeshes;
    std::vector<std::shared_ptr<const Animation::Skeleton>> skeletons;
    std::vector<std::shared_ptr<const Animation::AnimationClip>> animationClips;
    std::vector<std::shared_ptr<const MaterialData>> materials;

    /// <summary>
    /// 이 파일이 담은 모든 페이로드 범위이다. 종류별 열거를 한 곳에 모아
    /// <see cref="UnloadUnreferenced"/>가 오디오를 포함한 모든 페이로드의 외부 소유자를 검사하게 한다.
    /// </summary>
    [[nodiscard]] auto AllPayloadRanges() const
    {
        return std::tie(
            meshes, images, audioClips, skinnedMeshes, skeletons, animationClips, materials);
    }

    /// <summary>모든 페이로드 범위를 차례로 방문자에게 건넨다.</summary>
    /// <param name="visitor">범위 하나를 받는 호출 가능 객체다.</param>
    template <typename Visitor>
    void ForEachPayloadRange(Visitor&& visitor) const
    {
        std::apply(
            [&visitor](const auto&... ranges) { (visitor(ranges), ...); },
            AllPayloadRanges());
    }

    /// <summary>
    /// 이 파일의 페이로드 중 하나라도 데이터베이스 밖에서 쥐고 있는지이다. in-flight
    /// 프레임, 백엔드의 resolve된 리소스, 재생 중인 오디오 보이스가 그런 소유자다.
    /// </summary>
    [[nodiscard]] bool IsAnyPayloadHeldElsewhere() const;
};

// 페이로드 종류를 더하면 여기서 컴파일이 멎는다. LoadedFile은 모양이 같은 범위들만
// 담으므로 크기가 곧 종류의 수이고, AllPayloadRanges를 함께 고치라는 신호가 된다.
static_assert(
    sizeof(LoadedFile) == 7 * sizeof(std::vector<std::shared_ptr<const MeshData>>),
    "LoadedFile gained a payload kind; add it to LoadedFile::AllPayloadRanges as well.");

/// <summary>
/// 에셋 파일에서 임포트한 페이로드를 에셋 id별로 보관한다.
///
/// 표는 프로젝트가 무엇을 담는지 답하고, 이 캐시는 그중 무엇이 메모리에 상주하는지 답한다.
/// 매니페스트를 쓰는 빌드는 페이로드가 필요 없고, 페이로드를 쓰는 런타임은 매니페스트를 모른다.
///
/// <b>상주는 캐시이므로 등록 내용을 바꾸지 않는다.</b> 로드는 const이고 보관은 mutable이다.
/// 키는 에셋 표가 발급한 id를 사용한다.
/// </summary>
class AssetPayloadCache final
{
public:
    /// <summary>
    /// 이 에셋 파일의 페이로드를 돌려주고, 아직 없으면 소스에서 읽어 보관한다. 임포터가 없거나
    /// 읽기가 실패하면 null이다.
    /// </summary>
    [[nodiscard]] const LoadedFile* Load(
        const Asset& asset, const Platform::IContentSource& source) const;

    /// <summary>
    /// 아무도 참조하지 않는 파일의 페이로드를 버린다. 데이터베이스 밖에서 하나라도 쥐고 있는
    /// 파일은 남긴다 — in-flight 프레임, 백엔드의 resolve된 리소스, 재생 중인 오디오가 그렇다.
    /// </summary>
    void UnloadUnreferenced(const std::unordered_set<AssetKey>& referencedAssets);

    /// <summary>
    /// 앞선 캐시가 들고 있던 파일을 새 키로 물려받는다. 같은 파일임을 확인하는 일 — guid와 내용
    /// 해시를 견주는 일 — 은 표가 하고, 여기서는 옮기기만 한다.
    /// </summary>
    /// <returns>물려받을 것이 있었으면 true다.</returns>
    bool AdoptFrom(
        const AssetPayloadCache& previous, const Asset& before, const Asset& after);

    /// <summary>지금 보관 중인 파일 수다.</summary>
    [[nodiscard]] std::size_t GetLoadedCount() const { return mFiles.size(); }

    /// <summary>
    /// 이 에셋의 보관을 버린다. 파일 내용이 바뀌었을 때 표가 부른다 — id는 경로 해시라 내용이
    /// 바뀌어도 그대로이므로, 버리지 않으면 옛 내용의 페이로드가 계속 답으로 나온다.
    /// </summary>
    void Forget(const Asset& asset) { mFiles.erase(&asset); }

    /// <summary>보관을 전부 버린다. 표가 다시 스캔할 때 부른다.</summary>
    void Clear() { mFiles.clear(); }

private:
    /// <summary>에셋 id별 페이로드다. 로드가 등록 내용을 바꾸지 않으므로 mutable이다.</summary>
    mutable std::unordered_map<const Asset*, LoadedFile> mFiles;
};

}
