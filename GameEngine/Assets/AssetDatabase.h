#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../Animation/AnimationClip.h"
#include "../Animation/Skeleton.h"
#include "../Core/Guid.h"
#include "Asset.h"
#include "AssetPayloadCache.h"
#include "AssetReference.h"
#include "AudioData.h"
#include "MaterialData.h"
#include "MeshData.h"
#include "SkinnedMeshData.h"
#include "TextureData.h"
#include "../Platform/IContentSource.h"

namespace GameEngine::Assets
{

/// <summary>프로젝트 루트의 지원 에셋을 검색하고 빌드용 manifest로 직렬화한다.</summary>
class AssetDatabase final
{
public:
    static constexpr unsigned int CurrentFormatVersion = 6;
    static constexpr std::string_view ManifestRelativePath = "Assets/AssetDatabase.json";

    /// <summary>
    /// Registers every supported asset a content source holds — 현재 루트 .gameproject, Scene,
    /// PNG/JPEG 텍스처와 FBX 메시.
    ///
    /// 프로젝트는 소스가 담고 있는 그것이므로, 디렉터리든 프로젝트가 packed된 실행 파일이든
    /// 같은 코드로 refresh된다. 소스는 소유하지 않으며 데이터베이스보다 오래 살아야 한다.
    /// 호출자가 소스를 만드는 이유가 그것이다: 얼마나 오래 필요한지는 호출자만 안다.
    /// </summary>
    [[nodiscard]] bool Refresh(const Platform::IContentSource& source);

    /// <summary>초기화된 프로젝트 루트 안의 지원 에셋 하나를 등록하거나 갱신한다.</summary>
    [[nodiscard]] bool RegisterAsset(const std::filesystem::path& assetPath);

    /// <summary>빌드 결과가 사용할 데이터베이스 manifest를 결정적인 순서로 기록한다.</summary>
    [[nodiscard]] bool SaveManifest(const std::filesystem::path& manifestPath) const;

    /// <summary>기존 데이터베이스 manifest를 읽어 등록 정보를 복원한다. 소스는 소유하지 않는다.</summary>
    [[nodiscard]] bool LoadManifest(
        const Platform::IContentSource& source, const std::filesystem::path& manifestPath);

    [[nodiscard]] const Asset* FindAsset(AssetKey assetId) const;
    [[nodiscard]] const Asset* FindAsset(const std::filesystem::path& assetPath) const;

    /// <summary>
    /// 정체성으로 에셋을 찾는다. 그 guid를 가진 에셋이 이 프로젝트에 없으면 null이다.
    /// </summary>
    [[nodiscard]] const Asset* FindAsset(const Core::Guid& guid) const;

    /// <summary>
    /// 참조가 가리키는 것을 resolve하고, 아무것도 가리키지 않으면 null이다. 파일이 등록되어
    /// 있어야 하고 그 로컬 id 자리에 에셋을 담고 있어야 한다: 임포터가 몇 개를 담는지 기록해
    /// 두므로, 편집된 모델이 남긴 낡은 참조는 파일로 resolve되는 대신 여기서 실패한다.
    /// </summary>
    [[nodiscard]] const Asset* FindAsset(const AssetReference& reference) const;

    /// <summary>
    /// 이 경로를 사이드카로 갖는 에셋이다. 형식이 사이드카를 쓰지 않거나 어느 에셋의 사이드카
    /// 이름도 아니면 null이다. 이름으로 답한다 — 그 사이드카 파일이 실제로 있는지는 묻지 않는다.
    /// 콘텐츠 브라우저가 에셋의 메타데이터 파일을 목록에서 감출 때 이것으로 가른다.
    /// </summary>
    [[nodiscard]] const Asset* FindSidecarOwner(const std::filesystem::path& relativePath) const;

    /// <summary>
    /// 이 에셋이 이미 갖고 있는 사이드카의 프로젝트 상대 경로다. 없으면 비어 있다.
    ///
    /// 지금 이름과 옛 이름을 모두 보는 이유는, 옛 이름의 사이드카가 있는 에셋에 새 이름의 것을
    /// 하나 더 만들면 한 에셋에 설정이 둘이 되기 때문이다. 있는 것을 고칠 때도 그 파일을 고쳐야
    /// 하므로 예/아니오가 아니라 경로로 답한다.
    /// </summary>
    /// <param name="asset">이 데이터베이스에 등록된 에셋이다.</param>
    [[nodiscard]] std::filesystem::path GetSidecarPath(const Asset& asset) const;

    template <typename T>
    [[nodiscard]] const T* FindAsset(AssetKey assetId) const;

    template <typename T>
    [[nodiscard]] const T* FindAsset(const std::filesystem::path& assetPath) const;

    template <typename T>
    [[nodiscard]] const T* FindAsset(const AssetReference& reference) const;

    [[nodiscard]] const std::filesystem::path& GetProjectRootPath() const
    {
        return mProjectRootPath;
    }

    /// <summary>
    /// 이 프로젝트의 파일들이 오는 곳이다. 그것을 읽는 모든 것 — 임포터, 프론트엔드의 메시·
    /// 이미지 캐시 — 이 경로를 여는 대신 이것을 통해 읽으므로, 실행 파일에 packed된 프로젝트도
    /// 디렉터리에 놓인 프로젝트와 같은 방식으로 읽힌다.
    /// </summary>
    [[nodiscard]] const Platform::IContentSource& GetContentSource() const;

    /// <summary>
    /// 참조가 가리키는 메시이다. 상주하고 있지 않으면 로드한다. 참조가 메시를 가리키지 않거나
    /// 파일을 읽을 수 없으면 null이다.
    ///
    /// 모델은 한 번 파싱되어 안의 모든 메시를 내놓으므로, 파일의 두 번째 메시를 요청하면 전부
    /// 로드된다. 이는 선택이 아니라 포맷의 속성이다.
    /// </summary>
    [[nodiscard]] std::shared_ptr<const MeshData> LoadMesh(const AssetReference& reference) const;

    /// <summary>참조가 가리키는 텍스처이다. 상주하고 있지 않으면 로드한다.</summary>
    [[nodiscard]] std::shared_ptr<const TextureData> LoadTexture(
        const AssetReference& reference) const;

    /// <summary>
    /// 참조가 가리키는 오디오 클립이다. 상주하고 있지 않으면 로드한다.
    /// 클립 참조가 아니거나 파일 형식을 디코딩할 수 없으면 null이다.
    /// </summary>
    /// <param name="reference">클립을 가리키는 참조다.</param>
    /// <returns>재생 준비가 된 PCM이며, 없으면 null이다.</returns>
    [[nodiscard]] std::shared_ptr<const AudioData> LoadAudioClip(
        const AssetReference& reference) const;

    /// <summary>
    /// 참조가 가리키는 스킨드 메시이다. 상주하고 있지 않으면 로드한다. 참조가 스킨드 메시를
    /// 가리키지 않으면 null이다.
    /// </summary>
    [[nodiscard]] std::shared_ptr<const SkinnedMeshData> LoadSkinnedMesh(
        const AssetReference& reference) const;

    /// <summary>참조가 가리키는 골격이다. 상주하고 있지 않으면 로드한다.</summary>
    [[nodiscard]] std::shared_ptr<const Animation::Skeleton> LoadSkeleton(
        const AssetReference& reference) const;

    /// <summary>참조가 가리키는 애니메이션 클립이다. 상주하고 있지 않으면 로드한다.</summary>
    [[nodiscard]] std::shared_ptr<const Animation::AnimationClip> LoadAnimationClip(
        const AssetReference& reference) const;

    /// <summary>참조가 가리키는 머티리얼이다. 상주하고 있지 않으면 로드한다.</summary>
    [[nodiscard]] std::shared_ptr<const MaterialData> LoadMaterial(
        const AssetReference& reference) const;

    /// <summary>
    /// 주어진 에셋 집합 밖의 모든 로드된 페이로드를 내린다.
    ///
    /// 어느 에셋이 필요한지는 호출자의 답이다. 그 답을 줄 수 있는 것이 호출자뿐이기 때문이다:
    /// 이 데이터베이스가 페이로드의 유일한 영속 참조를 쥐고 있어서, 이 안에서 보면 로드된 모든
    /// 에셋이 안 쓰이는 것처럼 보인다 — 실행 중인 장면이 매 프레임 그리는 것들까지도.
    ///
    /// 상주는 바이트 예산이 아니라 참조되는 것으로 제한된다. 예산은 로드된 장면이 아직 그리는
    /// 것을 퇴거했다가 다시 로드하게 만들고, 그것은 절약이 아니라 공회전이다.
    ///
    /// 다른 무언가 — in-flight 프레임, 백엔드의 resolve된 리소스, 재생 중인 오디오 보이스 — 가
    /// 아직 쥔 페이로드는 이 호출을 살아남고 나중 호출이 수거한다.
    /// </summary>
    void UnloadUnreferenced(const std::unordered_set<AssetKey>& referencedAssets);

    /// <summary>몇 개의 페이로드가 상주 중인지이다. 진단과 테스트에 쓰인다.</summary>
    [[nodiscard]] std::size_t GetLoadedPayloadCount() const;

    /// <summary>
    /// 다른 데이터베이스가 이미 읽어 둔 페이로드를, 정체성과 내용이 그대로인 에셋에 한해
    /// 물려받는다.
    ///
    /// 파일이 하나 바뀌면 에디터는 새 데이터베이스에 스캔해 통째로 갈아 끼운다 — 그 자리에서
    /// 스캔하면 실패한 스캔이 자기를 비워, 복사 중 잠긴 파일 하나가 편집 중인 장면이 그리는
    /// 모든 것을 지우기 때문이다. 그 선택은 옳지만 대가가 있다: 새 데이터베이스에는 상주하던
    /// 것이 하나도 없어, 바뀌지 않은 이미지까지 다시 디코드되고 메시가 다시 파싱된다. 이 호출이
    /// 그 대가를 <b>실제로 바뀐 것</b>에만 물린다.
    ///
    /// 짝은 <b>경로가 아니라 guid</b>로 짓는다. 옮겨진 파일은 guid가 같고 경로만 다르므로,
    /// 경로로 짝지으면 파일을 옮기는 것만으로 그 페이로드를 버리게 된다 — 「이동은 정체성을
    /// 지킨다」가 페이로드에서도 참이려면 여기서도 guid여야 한다. guid가 없는 에셋은 짝지을
    /// 수 없으므로 그냥 다시 읽는다.
    ///
    /// <b>내용 해시가 다르면 물려받지 않는다.</b> 그것이 이 최적화가 거짓말이 되는 자리다:
    /// 바뀐 파일의 옛 픽셀을 계속 내놓으면, 사람은 자기가 방금 저장한 것이 반영되지 않는 것을
    /// 본다.
    /// </summary>
    /// <param name="previous">갈아 끼우기 전의 데이터베이스다. 그대로 둔다.</param>
    void InheritPayloadsFrom(const AssetDatabase& previous);

    [[nodiscard]] const std::vector<std::unique_ptr<Asset>>& GetAssets() const { return mAssets; }

    [[nodiscard]] static std::optional<AssetType> GetAssetType(
        const std::filesystem::path& relativePath);
    [[nodiscard]] static std::string_view GetAssetTypeName(AssetType assetType);
    /// <summary><see cref="GetAssetTypeName"/>의 역이다. 모르는 이름이면 비어 있다.</summary>
    [[nodiscard]] static std::optional<AssetType> ParseAssetTypeName(std::string_view name);

private:
    /// <summary>
    /// 에셋 하나를 등록한다. `presentFiles`는 스캔이 이미 열거한 파일들의 경로 키이며, 사이드카가
    /// 실제로 있는지를 파일시스템에 다시 묻지 않고 이것으로 답한다. 단건 등록은 목록이 없으므로
    /// null을 넘기고, 그때만 원본에 존재를 묻는다.
    /// </summary>
    [[nodiscard]] bool RegisterAssetInternal(
        const std::filesystem::path& assetPath,
        const std::unordered_set<std::string>* presentFiles = nullptr);

    [[nodiscard]] std::optional<std::filesystem::path> MakeRelativePath(
        const std::filesystem::path& assetPath) const;
    void SortAndRebuildIndexes();
    void Clear();

    std::filesystem::path mProjectRootPath;
    const Platform::IContentSource* mContentSource = nullptr;
    std::vector<std::unique_ptr<Asset>> mAssets;
    std::unordered_map<AssetKey, std::size_t> mAssetsById;
    std::unordered_map<std::string, std::size_t> mAssetsByPath;
    /// <summary>정체성별 에셋 색인이다. guid를 가진 에셋만 들어 있다.</summary>
    std::unordered_map<Core::Guid, std::size_t> mAssetsByGuid;
    /// <summary>사이드카 경로 키별 에셋 색인이다. 사이드카를 쓰는 형식의 에셋만 들어 있다.</summary>
    std::unordered_map<std::string, std::size_t> mAssetsBySidecarPath;

    /// <summary>
    /// 이 프로젝트에서 지금 메모리에 올라와 있는 페이로드다. 표와 나눠 든 이유는 아무도 둘을
    /// 함께 쓰지 않기 때문이다 — 매니페스트를 쓰는 빌드는 이것을 부르지 않고, 이것을 쓰는
    /// 런타임은 매니페스트를 모른다.
    /// </summary>
    AssetPayloadCache mPayloads;
};

template <typename T>
const T* AssetDatabase::FindAsset(const AssetKey assetId) const
{
    return dynamic_cast<const T*>(FindAsset(assetId));
}

template <typename T>
const T* AssetDatabase::FindAsset(const std::filesystem::path& assetPath) const
{
    return dynamic_cast<const T*>(FindAsset(assetPath));
}

template <typename T>
const T* AssetDatabase::FindAsset(const AssetReference& reference) const
{
    return dynamic_cast<const T*>(FindAsset(reference));
}

/// <summary>
/// 에셋 참조 하나가 지금 이 프로젝트에서 어떤 상태인지다.
///
/// 참조는 이름이라, 그 이름이 가리키는 것이 있는지는 물어봐야 안다. 인스펙터의 참조 칸은 자유
/// 입력이라 오타 한 글자가 "아무것도 가리키지 않는 참조"가 되는데, 그것이 화면에서 빈 참조와
/// 똑같이 보이면 사람은 "왜 안 보이지"를 혼자 오래 헤맨다. 이 구분이 그 차이를 말한다.
/// </summary>
enum class AssetReferenceStatus
{
    /// <summary>아무것도 가리키지 않는다. 일부러 비워 둔 상태이며 오류가 아니다.</summary>
    Empty,
    /// <summary>가리키는 에셋이 이 데이터베이스에 있다.</summary>
    Resolved,
    /// <summary>무언가를 가리키지만 그런 에셋이 없다. 오타이거나, 지워졌거나, 아직 임포트되지 않았다.</summary>
    Missing,
};

/// <summary>
/// 참조가 이 데이터베이스에서 해석되는지 판정한다. 빈 참조와 해석되지 않는 참조를 구분하는
/// 것이 요점이다 — 앞의 것은 사람의 결정이고, 뒤의 것은 사람이 모르고 있는 사실이다.
/// </summary>
/// <param name="database">해석할 프로젝트의 에셋 데이터베이스다.</param>
/// <param name="reference">판정할 참조다.</param>
[[nodiscard]] AssetReferenceStatus ClassifyAssetReference(
    const AssetDatabase& database, const AssetReference& reference);

/// <summary>
/// 이 에셋을 가리키는 참조다. 파일의 대표 에셋을 가리킨다.
///
/// 참조 칸은 아직 손으로 적는 자리라, 사람이 경로를 옮겨 적다 틀린다. 그 경로를 사람이 짓지
/// 않고 에셋 자신에게 물으면 틀릴 자리가 없다 — 콘텐츠 브라우저의 "참조 복사"가 그 길이고,
/// 여기서 나온 참조는 같은 데이터베이스에서 반드시 해석된다.
/// </summary>
[[nodiscard]] AssetReference MakeAssetReference(
    const Asset& asset, AssetReferenceForm form = AssetReferenceForm::Path);


/// <summary>
/// 이 참조를 사람에게 보일 글자로 옮긴다. 가리키는 에셋이 있으면 그 경로이고, 파일 안의 것이면
/// 어느 것인지 이름이 붙는다. 해석되지 않으면 저장 형식 그대로 보인다 — 그것이 무엇이 잘못됐는지
/// 말해 주는 유일한 단서이기 때문이다.
///
/// <see cref="AssetReference::ToString"/>과 갈라져 있는 이유는 그 함수가 파일에 적히는 형식이기
/// 때문이다. 정체성으로 저장되기 시작하면 그 글자는 32자리 16진수가 되는데, 인스펙터 칸도 선택
/// 목록도 로그도 그것을 보여서는 사람이 무엇을 가리키는지 읽을 수 없다.
/// </summary>
/// <param name="database">해석할 프로젝트의 에셋 데이터베이스다.</param>
/// <param name="reference">보일 참조다.</param>
[[nodiscard]] std::string DescribeAssetReference(
    const AssetDatabase& database, const AssetReference& reference);
/// <summary>
/// 이 참조가 가리키는 에셋이 그 종류인지 판정한다. 종류를 말하지 않는 자리 — `assetType`이 빈
/// 경우 — 는 해석되기만 하면 받는다.
///
/// 에셋 참조를 받는 자리가 무엇을 받을 수 있는지 묻는 곳이 여럿이라 여기 있다: 인스펙터의 선택
/// 목록이 이 규칙으로 걸러지고, 끌어 놓기가 같은 규칙으로 받을지를 정한다. 두 곳이 각자 답하면
/// 목록에 없는 것을 끌어다 놓을 수 있게 된다.
/// </summary>
/// <param name="database">해석할 프로젝트의 에셋 데이터베이스다.</param>
/// <param name="reference">판정할 참조다.</param>
/// <param name="assetType">요구하는 종류이며, 비어 있으면 종류를 묻지 않는다.</param>
/// <returns>그 자리가 이 참조를 받을 수 있으면 true다.</returns>
[[nodiscard]] bool AssetReferenceMatchesType(
    const AssetDatabase& database,
    const AssetReference& reference,
    std::optional<AssetType> assetType);

/// <summary>선택 목록에 놓일 에셋 하나다: 가리킬 참조와 사람이 읽을 이름.</summary>
struct AssetChoice
{
    AssetReference reference;
    /// <summary>프로젝트 상대 경로다. 파일 안의 에셋이면 `Meshes/Model.fbx#1 (name)`처럼 어느 것인지 붙는다.</summary>
    std::string label;
};

/// <summary>
/// 이 데이터베이스에서 주어진 종류의 에셋을 전부 모은다. 경로 순이며, 파일 하나에 여러 에셋이
/// 들어 있으면 — 모델의 메시들 — 로컬 id 순으로 각각 하나씩이다. 인스펙터의 에셋 선택 목록이
/// 여기서 나온다.
/// </summary>
/// <param name="database">모을 프로젝트의 데이터베이스다.</param>
/// <param name="type">원하는 에셋 종류다.</param>
/// <param name="form">고른 것을 어떤 형식으로 가리킬지다.</param>
/// <returns>그 종류의 선택지들이다. 없으면 비어 있다.</returns>
[[nodiscard]] std::vector<AssetChoice> CollectAssetChoices(
    const AssetDatabase& database, AssetType type,
    AssetReferenceForm form = AssetReferenceForm::Path);

}
