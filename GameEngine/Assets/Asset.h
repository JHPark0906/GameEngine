#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../Core/Guid.h"
#include "../Core/Json.h"



namespace GameEngine::Assets
{

/// <summary>
/// 에셋마다 하나씩인 안정된 수다. <b>정체성이 아니다</b> — 정체성은 사이드카의 guid이고,
/// 그것으로 찾는 길은 <see cref="AssetDatabase::FindAsset"/>의 guid 오버로드다.
///
/// 이 값이 하는 일은 둘뿐이다. 하나는 <see cref="ImportIdentity::MakeResourceId"/>의 씨앗
/// (<c>assetId ^ contentHash ^ localId</c>)으로서 GPU 리소스 id를 만드는 것이고, 다른 하나는
/// 표가 등록과 매니페스트 로드에서 충돌을 거절하는 검사의 키다. 그 검사가 곧 씨앗의 보호막이다:
/// 두 에셋이 같은 값에 같은 내용 해시를 가지면 리소스 id가 겹친다.
///
/// 값이 어떻게 만들어지는지는 에셋에 정체성이 있느냐에 따라 갈린다 — 있으면 guid를 접고, 없으면
/// 경로를 해시한다(<c>AssetDatabase.cpp</c>의 등록 경로). 그 두 갈래를 보면 정체성처럼 보이기
/// 쉽지만, <b>위의 두 쓰임에는 정체성일 필요가 없고 「에셋마다 다른 안정된 수」면 충분하다.</b>
/// 정체성 없는 에셋도 이 값을 갖는 것이 그래서 옳다 — 그것을 guid로 좁히면 그 에셋들이 충돌
/// 검사 밖으로 나간다.
/// </summary>
using AssetKey = std::uint64_t;

/// <summary>
/// 사이드카 파일의 첫 키에 적히는 판본이다. 읽는 쪽이 나머지를 믿기 전에 무엇을 쥐고 있는지
/// 알 수 있게 한다.
/// </summary>
inline constexpr std::string_view MetaFormat = "gameengine-meta/1";

/// <summary>
/// 사이드카 파일이 가질 수 있는 이름의 꼬리들이다. 현재 이름이 먼저이고, 그 뒤가 개명 전 이름이다.
///
/// 목록이 필요한 이유는 <b>주인 없는 사이드카를 찾는</b> 일 때문이다. 주인이 있는 사이드카는 그
/// 에셋에게 자기 꼬리를 물어보면 되지만, 주인이 사라진 파일은 물어볼 에셋이 없어서 「이 파일이
/// 사이드카인가」를 이름만으로 판정해야 한다.
/// </summary>
inline constexpr std::string_view SidecarSuffixes[]{ ".meta", ".sprite.json" };

/// <summary>
/// 정리 대상으로 옆으로 치워 둔 사이드카가 받는 꼬리다.
///
/// 지우지 않고 이름만 바꾸는 자리라, 되돌리는 일이 이름을 되돌리는 것으로 끝난다. 이 꼬리가 붙은
/// 파일은 에셋 종류가 없어 스캔이 지나치고 사이드카로도 읽히지 않으므로, 치운 순간부터 조용하다.
/// </summary>
inline constexpr std::string_view OrphanedSidecarSuffix = ".orphan";


enum class AssetType : unsigned char
{
    ProjectSettings,
    Scene,
    Sprite,
    Mesh,
    Font,
    AudioClip,
    Icon,
    Skeleton,
    AnimationClip,
    /// <summary>
    /// 뼈 영향을 가진 메시다. <see cref="Mesh"/>와 종류를 나누는 이유는 둘의 페이로드 타입이
    /// 다르기 때문이다(<c>MeshData</c> 대 <c>SkinnedMeshData</c>) — 한 모델 파일이 인스펙터의
    /// 선택 목록에서 둘을 섞으면, 목록을 보는 사람은 어느 것이 뼈에 매였는지 이름 말고는 알
    /// 방법이 없다.
    /// </summary>
    SkinnedMesh,
    /// <summary>
    /// 텍스처와 tint를 묶는 표면 성질이다. 위의 것들과 달리 소스 파일에서 임포트되지 않는다 —
    /// <c>.material</c> 파일 자체가 사람이 만드는 에셋이고, 그 파일의 JSON이 페이로드
    /// 전부다.
    /// </summary>
    Material,
};

/// <summary>
/// 파일 안의 에셋 하나이다. 목록에서의 위치가 로컬 id이고 `AssetReference`가 저장하는 것이
/// 그것이므로, 임포터가 만들어 내는 순서는 매 실행 같아야 한다.
///
/// 대부분의 파일은 파일 이름을 딴 에셋을 정확히 하나 담는다. 모델 파일은 메시마다 하나를
/// 담는데, 이것이 존재하는 이유가 바로 그 경우다: 이것이 없으면 파일에 메시가 몇 개인지 말할
/// 수 없고, 파일의 두 번째 메시를 가리키는 참조를 검사하거나 제시할 수도 없다.
/// </summary>
struct SubAsset
{
    AssetType type = AssetType::Mesh;
    std::string name;
};

/// <summary>원본 파일과 안정적인 프로젝트 상대 경로를 소유하는 모든 에셋의 기반 형식이다.</summary>
class Asset
{
public:
    virtual ~Asset() = default;

    Asset(const Asset&) = delete;
    Asset& operator=(const Asset&) = delete;
    Asset(Asset&&) = default;
    Asset& operator=(Asset&&) = default;

    [[nodiscard]] AssetKey GetId() const { return mId; }

    /// <summary>
    /// 이 에셋의 정체성이다. 사이드카가 들고 있으며 파일이 옮겨져도 바뀌지 않는다. 사이드카가
    /// 없거나 아직 발급받지 못했으면 비어 있고, 그런 에셋은 guid 참조로 가리킬 수 없다.
    /// </summary>
    [[nodiscard]] const Core::Guid& GetGuid() const { return mGuid; }

    /// <summary>
    /// 정체성을 기록한다. 데이터베이스만 이것을 호출한다: guid는 사이드카에서 읽히거나 발급되는
    /// 등록의 산출물이고, `Asset`은 그 외에는 만들어진 뒤 고정이다.
    /// </summary>
    void SetGuid(const Core::Guid guid) { mGuid = guid; }

    [[nodiscard]] AssetType GetType() const { return mType; }
    [[nodiscard]] const std::filesystem::path& GetRelativePath() const { return mRelativePath; }
    [[nodiscard]] const std::filesystem::path& GetSourcePath() const { return mSourcePath; }
    [[nodiscard]] std::uint64_t GetContentHash() const { return mContentHash; }
    [[nodiscard]] std::uintmax_t GetFileSize() const { return mFileSize; }

    /// <summary>
    /// 이 파일이 담은 에셋들이다. 로컬 id 순이다. 임포터가 읽지 못한 파일은 하나도 갖지 않으며,
    /// 이는 임포트할 것이 정말로 없는 파일과 구별된다.
    /// </summary>
    [[nodiscard]] const std::vector<SubAsset>& GetSubAssets() const { return mSubAssets; }

    /// <summary>
    /// 임포터가 찾아낸 것을 기록한다. 데이터베이스만 이것을 호출한다: 이 목록은 임포트의
    /// 출력이고, `Asset`은 그 외에는 만들어진 뒤 고정이다.
    /// </summary>
    void SetSubAssets(std::vector<SubAsset> subAssets) { mSubAssets = std::move(subAssets); }
    /// <summary>
    /// 이 에셋의 사이드카가 원본 이름 뒤에 붙이는 접미사다. 모든 형식이 <c>.meta</c> 하나를
    /// 쓴다 — 어느 형식의 메타인지는 원본의 확장자가 말하므로 이름에 종류가 들어가지 않는다.
    /// </summary>
    [[nodiscard]] virtual std::string_view GetSidecarSuffix() const { return ".meta"; }

    /// <summary>
    /// 이 에셋에 사이드카가 없을 때 놓을 기본 내용이다. 판본과 건네받은 정체성은 모든 형식이
    /// 담고, 형식마다 자기 항목을 더한다.
    ///
    /// 정체성을 만들지 않고 받는 이유는 발급이 되돌릴 수 없는 동작이기 때문이다. 그것을 부르는
    /// 자리는 하나여야 하고, 내용을 답하는 이 함수는 그 자리가 아니다.
    ///
    /// 내용을 값으로 답할 뿐 파일을 만들지 않는다. 콘텐츠 소스는 읽기 전용이고 런타임은 프로젝트에
    /// 쓰지 않으므로, 디스크에 놓는 것은 에디터의 일이다.
    /// </summary>
    /// <param name="guid">이 에셋에 주어진 정체성이다.</param>
    [[nodiscard]] virtual Core::Json MakeDefaultSidecar(Core::Guid guid) const;

    /// <summary>
    /// 호환용 사이드카 접미사다. 비어 있으면 대체 이름이 없다.
    ///
    /// 데이터베이스는 기본 이름을 먼저 찾고, 없을 때만 이 접미사의 사이드카를 읽어 그 사실을
    /// 로그로 알린다. 저장소 밖의 프로젝트도 같은 호환 규칙을 따른다.
    /// </summary>
    [[nodiscard]] virtual std::string_view GetLegacySidecarSuffix() const { return {}; }

    /// <summary>
    /// 사람이 손으로 쓰는 사이드카를 읽어 적용한다. 적지 않은 항목은 기본값으로 남는다. 내용이
    /// 잘못됐으면 <see cref="Core::JsonError"/>를 던지고, 그때 이 에셋은 기본값으로 등록된다 —
    /// 사이드카 하나가 깨졌다고 프로젝트가 열리지 않아서는 안 되기 때문이다.
    /// </summary>
    virtual void ReadSidecarMetadata(const Core::Json& json) { static_cast<void>(json); }

    /// <summary>
    /// 매니페스트에 이 형식만의 항목을 싣는다. 기본은 아무것도 싣지 않는다.
    /// </summary>
    virtual void WriteManifestMetadata(Core::Json::Object& members) const
    {
        static_cast<void>(members);
    }

    /// <summary>
    /// <see cref="WriteManifestMetadata"/>가 실은 것을 되읽는다. 사이드카와 달리 매니페스트는
    /// 빌드가 만든 파일이라, 있어야 할 항목이 없으면 손상이므로 <see cref="Core::JsonError"/>를
    /// 던진다 — 조용히 기본값으로 읽으면 모든 스프라이트가 잘못된 배율로 그려진다.
    /// </summary>
    virtual void ReadManifestMetadata(const Core::Json& json) { static_cast<void>(json); }

protected:
    Asset(
        AssetKey id,
        AssetType type,
        std::filesystem::path relativePath,
        std::filesystem::path sourcePath,
        std::uint64_t contentHash,
        std::uintmax_t fileSize);

private:
    AssetKey mId = 0;
    Core::Guid mGuid;
    AssetType mType = AssetType::ProjectSettings;
    std::filesystem::path mRelativePath;
    std::filesystem::path mSourcePath;
    std::uint64_t mContentHash = 0;
    std::uintmax_t mFileSize = 0;
    std::vector<SubAsset> mSubAssets;
};

/// <summary>프로젝트 루트의 .gameproject 설정 파일이다.</summary>
class ProjectSettingsAsset final : public Asset
{
public:
    ProjectSettingsAsset(AssetKey id, std::filesystem::path relativePath,
        std::filesystem::path sourcePath, std::uint64_t contentHash, std::uintmax_t fileSize);
};

/// <summary>장면 파일이다.</summary>
class SceneAsset final : public Asset
{
public:
    SceneAsset(AssetKey id, std::filesystem::path relativePath,
        std::filesystem::path sourcePath, std::uint64_t contentHash, std::uintmax_t fileSize);
};

/// <summary>이미지 파일이다. pixels-per-unit과 nine-slice 테두리 메타데이터를 함께 지닌다.</summary>
class Sprite final : public Asset
{
public:
    struct Border
    {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
    };

    /// <summary>
    /// 이미지를 프레임으로 나누는 격자다. 스프라이트 시트가 한 장의 이미지에 여러 그림을
    /// 담는 방식이 이것이고, 애니메이션은 그 프레임들을 차례로 보여 준다.
    ///
    /// 프레임 사각형 목록 대신 격자인 이유는 시트가 실제로 그렇게 만들어지기 때문이다: 셀
    /// 크기가 일정한 시트는 열·행 두 수로 온전히 기술되고, 사이드카에 손으로 적기도 쉽다.
    /// 불규칙한 프레임이 필요해지면 그때 목록을 더하면 되고, 격자는 그 목록의 특수한 경우로
    /// 남는다.
    /// </summary>
    struct Sheet
    {
        /// <summary>가로로 놓인 셀 수다. 1이면 나누지 않은 것과 같다.</summary>
        int columns = 1;
        /// <summary>세로로 놓인 셀 수다.</summary>
        int rows = 1;
        /// <summary>
        /// 실제로 쓰는 프레임 수다. 격자의 마지막 줄이 덜 찼을 때 빈 칸을 보여 주지 않는
        /// 방법이며, 0이면 columns * rows 전부다.
        /// </summary>
        int frameCount = 0;
        /// <summary>초당 프레임 수다. 클립이 자기 값을 말하지 않을 때의 기본 속도다.</summary>
        float frameRate = 12.0f;

        /// <summary>이 시트가 실제로 담은 프레임 수다. 언제나 1 이상이다.</summary>
        [[nodiscard]] int GetFrameCount() const;

        [[nodiscard]] bool operator==(const Sheet&) const = default;
    };

    Sprite(AssetKey id, std::filesystem::path relativePath,
        std::filesystem::path sourcePath, std::uint64_t contentHash, std::uintmax_t fileSize,
        float pixelsPerUnit = 100.0f, Border border = {}, Sheet sheet = {});

    [[nodiscard]] float GetPixelsPerUnit() const { return mPixelsPerUnit; }
    [[nodiscard]] const Border& GetBorder() const { return mBorder; }
    [[nodiscard]] bool HasBorder() const;

    /// <summary>이 이미지를 프레임으로 나누는 격자다. 사이드카가 말하지 않았으면 한 장 전체다.</summary>
    [[nodiscard]] const Sheet& GetSheet() const { return mSheet; }

    /// <summary>
    /// 프레임 하나가 이미지에서 차지하는 자리다. 0..1의 정규화 좌표이며 원점은 좌상단이라,
    /// 렌더링이 UV로 그대로 쓴다. 범위 밖 인덱스는 프레임 수로 감싸므로, 애니메이션이 프레임을
    /// 넘겨도 빈 그림이 되지 않는다.
    /// </summary>
    /// <param name="frameIndex">보여 줄 프레임 번호다. 0이 첫 프레임이다.</param>
    /// <param name="u">프레임 왼쪽의 정규화 좌표를 받는다.</param>
    /// <param name="v">프레임 위쪽의 정규화 좌표를 받는다.</param>
    /// <param name="width">프레임의 정규화 너비를 받는다.</param>
    /// <param name="height">프레임의 정규화 높이를 받는다.</param>
    void GetFrameRect(int frameIndex, float& u, float& v, float& width, float& height) const;

    /// <summary>
    /// 호환용 사이드카 이름은 <c>&lt;원본&gt;.sprite.json</c>이다.
    /// </summary>
    [[nodiscard]] std::string_view GetLegacySidecarSuffix() const override
    {
        return ".sprite.json";
    }

    [[nodiscard]] Core::Json MakeDefaultSidecar(Core::Guid guid) const override;
    void ReadSidecarMetadata(const Core::Json& json) override;
    void WriteManifestMetadata(Core::Json::Object& members) const override;
    void ReadManifestMetadata(const Core::Json& json) override;

private:
    float mPixelsPerUnit = 100.0f;
    Border mBorder;
    Sheet mSheet;
};


/// <summary>
/// 모델 파일이다. 안에 담긴 메시·스켈레톤·애니메이션 클립들이 sub-asset으로 나열된다.
/// </summary>
class Mesh final : public Asset
{
public:
    Mesh(AssetKey id, std::filesystem::path relativePath,
        std::filesystem::path sourcePath, std::uint64_t contentHash, std::uintmax_t fileSize);
};

/// <summary>
/// 모델 파일 안의 뼈대다. 스켈레톤 자체는 모델 파일을 벗어나지 않으므로, 이 형식은 언제나
/// <see cref="Mesh"/>와 같은 파일을 가리키는 서브에셋으로만 나타난다.
/// </summary>
class Skeleton final : public Asset
{
public:
    Skeleton(AssetKey id, std::filesystem::path relativePath,
        std::filesystem::path sourcePath, std::uint64_t contentHash, std::uintmax_t fileSize);
};

/// <summary>
/// 모델 파일 안의 애니메이션 클립이다. <see cref="Skeleton"/>과 마찬가지로 언제나 모델 파일의
/// 서브에셋으로만 나타난다.
/// </summary>
class AnimationClip final : public Asset
{
public:
    AnimationClip(AssetKey id, std::filesystem::path relativePath,
        std::filesystem::path sourcePath, std::uint64_t contentHash, std::uintmax_t fileSize);
};

/// <summary>
/// 모델 파일 안의, 뼈 영향을 가진 메시다. <see cref="Skeleton"/>과 마찬가지로 언제나 모델
/// 파일의 서브에셋으로만 나타난다.
/// </summary>
class SkinnedMesh final : public Asset
{
public:
    SkinnedMesh(AssetKey id, std::filesystem::path relativePath,
        std::filesystem::path sourcePath, std::uint64_t contentHash, std::uintmax_t fileSize);
};

/// <summary>폰트 파일이다.</summary>
class Font final : public Asset
{
public:
    Font(AssetKey id, std::filesystem::path relativePath,
        std::filesystem::path sourcePath, std::uint64_t contentHash, std::uintmax_t fileSize);
};

/// <summary>
/// 오디오 파일이다. 디코딩된 클립은 런타임 오디오 시스템이 재생한다.
/// </summary>
class AudioClip final : public Asset
{
public:
    AudioClip(AssetKey id, std::filesystem::path relativePath,
        std::filesystem::path sourcePath, std::uint64_t contentHash, std::uintmax_t fileSize);
};

/// <summary>
/// 실행 파일과 창의 얼굴이 될 <c>.ico</c>다. 폰트와 같은 모양이다 — 파일 자체가 곧 에셋이고
/// 확장자가 이미 종류를 답한다.
/// </summary>
class Icon final : public Asset
{
public:
    Icon(AssetKey id, std::filesystem::path relativePath,
        std::filesystem::path sourcePath, std::uint64_t contentHash, std::uintmax_t fileSize);
};

/// <summary>
/// <c>.material</c> 파일이다. 폰트·아이콘과 같은 모양이다 — 파일 자체가 곧 에셋이고 확장자가
/// 이미 종류를 답한다. 그 JSON을 실제 <see cref="MaterialData"/>로 바꾸는 일은 임포터가 한다.
/// </summary>
class Material final : public Asset
{
public:
    Material(AssetKey id, std::filesystem::path relativePath,
        std::filesystem::path sourcePath, std::uint64_t contentHash, std::uintmax_t fileSize);
};

}
