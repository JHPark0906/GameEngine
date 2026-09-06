#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Asset.h"
#include "AudioData.h"
#include "MaterialData.h"
#include "MeshData.h"
#include "SkinnedMeshData.h"
#include "TextureData.h"
#include "../Animation/AnimationClip.h"
#include "../Animation/Skeleton.h"
#include "ResourceId.h"

namespace GameEngine::Assets
{

/// <summary>
/// 지금 무엇이 임포트되고 있는지이다. 페이로드에 백엔드가 GPU 업로드를 캐시할 정체성을 주기
/// 위해 존재한다.
///
/// 콘텐츠 해시가 들어 있는 이유는 파일이 바뀌면 정체성도 바뀌어야 하기 때문이다. 경로만으로
/// 만든 id는 편집 뒤에도 그대로라서, 백엔드는 이전 내용으로 업로드한 것을 계속 그렸을 것이다 —
/// 에디터의 refresh가 정확히 그 일이 일어나는 순간이다.
/// </summary>
struct ImportIdentity
{
    AssetKey assetId = 0;
    std::uint64_t contentHash = 0;

    /// <summary>
    /// 이 로컬 id 자리 에셋의 id이다. 서로 다른 종류끼리는 결코 충돌하지 않는다 — 종류가 id의
    /// 상위 비트를 차지하기 때문이다.
    ///
    /// 같은 종류 안에서는 <b>해시</b>다. 셋을 XOR한 값이 하위 56비트로 잘려 들어가므로 충돌이
    /// 불가능하지는 않고, 그 확률은 한 종류 안 리소스 R개에 대해 대략 R²/2⁵⁷이다(백만 개에서
    /// 7e-6). 같은 비트를 쓰는 계수기와 해시의 요구가 다르다는 것은
    /// <see cref="Assets::MakeResourceId"/>의 주석에 적혀 있다.
    /// </summary>
    [[nodiscard]] std::uint64_t MakeResourceId(
        Assets::ResourceIdDomain domain, std::uint32_t localId) const
    {
        return Assets::MakeResourceId(domain, assetId ^ contentHash ^ localId);
    }
};

/// <summary>
/// 임포트에 파일의 얼마만큼을 요구하는지이다.
///
/// 프로젝트가 무엇을 담고 있는지 알아내는 일과 그것을 로드하는 일은 비용이 크게 다른 별개의
/// 일이다. 스캔에는 이름과 개수만 필요하다. 모델을 뺀 모든 포맷은 파일을 읽을 필요조차 없고,
/// 모델은 파싱은 필요하지만 정점 변환은 필요 없다. 이미지 디코딩은 실제 페이로드를
/// 요청할 때 수행해 스캔 중 불필요한 픽셀 메모리를 할당하지 않는다.
/// </summary>
enum class ImportMode : unsigned char
{
    /// <summary>이름, 종류, 개수만이다. 페이로드는 만들지 않는다.</summary>
    Structure,
    /// <summary>전부이다. 프레임이 실어 나를 수 있는 상태로 만든다.</summary>
    Full,
};

/// <summary>파일이 담은 에셋들과, full 임포트가 요구했다면 그 페이로드들이다.</summary>
struct ImportedContents
{
    std::vector<SubAsset> subAssets;

    /// <summary>
    /// sub-asset마다 한 항목씩, 같은 순서이며, structure 임포트 뒤에는 비어 있다. 모델은 한 번
    /// 파싱되어 안의 모든 메시를 내놓으므로, 파일의 두 번째 메시를 로드하면 전부 로드된다 —
    /// 하나씩이 아니라 함께 돌아오는 이유다.
    /// </summary>
    std::vector<std::shared_ptr<const MeshData>> meshes;
    std::vector<std::shared_ptr<const TextureData>> images;
    std::vector<std::shared_ptr<const AudioData>> audioClips;

    /// <summary>
    /// 위 셋과 같은 규칙이되, 한 파일이 서로 다른 종류의 서브에셋을 함께 낼 수 있는 자리에서만
    /// (모델 파일의 정적 메시·스킨드 메시·골격·클립) 사용된다: 인덱스는 언제나 subAssets와
    /// 나란하므로, 이 자리의 종류가 아닌 인덱스는 비어 있는 채로 셋 다 subAssets와 같은 길이다.
    /// AssetDatabase::LoadMesh 등이 참조의 로컬 id로 곧장 색인하는 계약이 그것을 요구한다.
    /// </summary>
    std::vector<std::shared_ptr<const SkinnedMeshData>> skinnedMeshes;
    std::vector<std::shared_ptr<const Animation::Skeleton>> skeletons;
    std::vector<std::shared_ptr<const Animation::AnimationClip>> animationClips;
    std::vector<std::shared_ptr<const MaterialData>> materials;
};

/// <summary>
/// 파일을 그 안에 담긴 에셋들로 바꾼다.
///
/// 파일이 곧 에셋이고 종류가 확장자 switch에서 나오는 것은 파일이 정확히 하나만 담는 동안에만
/// 통한다: 모델 파일은 여러 메시를 담고, 몇 개인지는 파싱해야만 알 수 있다. 임포터는 둘 다 아는
/// 유일한 자리다 — 확장자가 어떤 종류를 내놓는지, 그리고 특정 파일에 몇 개가 들었는지 — 그래서
/// 포맷 추가는 데이터베이스·에디터·런타임 캐시에 흩어진 편집이 아니라 등록 한 번이 된다.
///
/// 임포트는 프레임이 그릴 때가 아니라 데이터베이스가 refresh될 때 실행된다.
/// </summary>
class IAssetImporter
{
public:
    virtual ~IAssetImporter() = default;

    IAssetImporter(const IAssetImporter&) = delete;
    IAssetImporter& operator=(const IAssetImporter&) = delete;

    /// <summary>
    /// 이 임포터가 내놓는 에셋의 종류이다. 데이터베이스는 무엇을 열기도 전에 — 파일이 에셋이기는
    /// 한지 결정하려고 — 이것이 필요하므로, 파일 내용에 의존할 수 없다.
    /// </summary>
    [[nodiscard]] virtual AssetType GetAssetType() const = 0;

    /// <summary>
    /// 이 파일 안의 에셋들이다. 로컬 id 순이다. false 반환은 파일을 이해할 수 없었다는 뜻이고,
    /// 그때 `error`가 이유를 말한다. 실패해도 refresh 전체를 실패시키는 대신 파일을 안이 빈
    /// 채로 등록해 둔다. 읽을 수 없는 모델 하나가 프로젝트 열기를 막아서는 안 되기 때문이다.
    ///
    /// 바이트가 건네지므로 임포터는 결코 파일을 열지 않고, 실행 파일에 packed된 프로젝트도
    /// 디렉터리에 놓인 것과 똑같이 임포트된다. 경로가 따라오는 이유는 에셋 이름을 파일에서 따는
    /// 포맷에 필요해서이지, 열기 위해서가 아니다.
    /// </summary>
    [[nodiscard]] virtual bool Import(
        const std::filesystem::path& relativePath,
        std::span<const std::byte> fileBytes,
        const ImportIdentity& identity,
        ImportMode mode,
        ImportedContents& contents,
        std::string& error) const = 0;

protected:
    IAssetImporter() = default;
};

}
