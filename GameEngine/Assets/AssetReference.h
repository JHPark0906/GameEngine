#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "../Core/Guid.h"

namespace GameEngine::Assets
{

/// <summary>
/// 새로 만드는 참조를 무엇으로 적을지다.
///
/// 형식이 선택인 이유는 프로젝트마다 상태가 다르기 때문이다. 장면들이 아직 경로로 가리키고 있는
/// 프로젝트에 정체성 참조를 하나 섞어 넣으면 그 장면에 두 형식이 함께 살게 되고, 그것은 읽히기는
/// 하지만 사람이 파일을 보고 무엇이 규칙인지 알 수 없는 상태다.
/// </summary>
enum class AssetReferenceForm
{
    /// <summary>
    /// 프로젝트 상대 경로로 적는다.
    /// </summary>
    Path,
    /// <summary>
    /// 에셋의 정체성으로 적는다.
    /// </summary>
    Identity,
};

/// <summary>
/// 에셋을 가리키는 이름이다: 그것을 담은 파일과, 그 파일 안의 어느 에셋인지이다.
///
/// 에셋의 정체성이 경로뿐이면 "이 파일의 두 번째 메시"를 말할 방법이 없다. 그것으로 충분한 것은
/// 정확히 하나만 담는 포맷뿐인데, 모델 파일은 여러 메시를 담는다. 경로 옆에 로컬 id를 실어
/// 나르는 것이 그 말을 가능하게 하고, 런타임 컴포넌트가 `std::filesystem::path`가 아니라 이것을
/// 쥐는 이유이기도 하다: 경로는 위치이고 참조는 이름이다.
///
/// 모델 파일은 담고 있는 메시마다 에셋 하나를 내놓으므로, 로컬 id가 그중 하나를 고른다. 암묵적
/// "파일 전체" 에셋은 없다: 로컬 id 0은 파일의 첫 메시이지 전부를 합쳐 놓은 것이 아니다. 여러
/// 메시를 담은 파일을 Unity가 모델링하는 방식이 그렇다. 따라서 그런 파일의 모든 메시를 그리려면
/// 엔진이 모델을 자체 계층으로 표현할 수 있게 될 때까지는 메시마다 렌더러 하나가 필요하다.
/// </summary>
class AssetReference final
{
public:
    /// <summary>
    /// 더 말하지 않았을 때 파일이 가리키는 에셋이다 — 메시 하나짜리 모델 파일의 그 메시,
    /// 텍스처 파일의 그 이미지.
    /// </summary>
    static constexpr std::uint32_t MainAssetLocalId = 0;

    AssetReference() = default;

    explicit AssetReference(
        std::filesystem::path path, std::uint32_t localId = MainAssetLocalId);
    /// <summary>
    /// 정체성으로 가리키는 참조를 만든다. 이렇게 만든 참조는 파일이 옮겨져도 같은 것을 가리킨다.
    /// </summary>
    /// <param name="guid">가리킬 에셋의 정체성이다.</param>
    /// <param name="localId">파일 안에서 몇 번째 에셋인지다.</param>
    explicit AssetReference(Core::Guid guid, std::uint32_t localId = MainAssetLocalId);

    /// <summary>
    /// 이 참조가 정체성으로 가리키는지다. 거짓이면 경로로 가리킨다 — 두 형식이 함께 쓰이는
    /// 동안의 구분이며, 장면 파일이 정체성으로 옮겨 가면 경로 형식은 옛 파일에만 남는다.
    /// </summary>
    [[nodiscard]] bool IsGuidReference() const { return mGuid.IsValid(); }

    /// <summary>가리키는 정체성이다. 경로로 가리키는 참조에서는 비어 있다.</summary>
    [[nodiscard]] const Core::Guid& GetGuid() const { return mGuid; }


    [[nodiscard]] const std::filesystem::path& GetPath() const { return mPath; }
    [[nodiscard]] std::uint32_t GetLocalId() const { return mLocalId; }

    /// <summary>이 참조가 무언가를 가리키기는 하는지 여부이다. 빈 참조는 오류가 아니다.</summary>
    [[nodiscard]] bool IsValid() const { return !mPath.empty() || mGuid.IsValid(); }

    /// <summary>파일 내부의 무언가가 아니라 파일의 대표 에셋을 가리키는지 여부이다.</summary>
    [[nodiscard]] bool IsMainAsset() const { return mLocalId == MainAssetLocalId; }

    /// <summary>
    /// 장면 파일에 나타나는 그대로의 참조이다: 대표 에셋은 Meshes/Model.fbx,
    /// 내부의 것은 Meshes/Model.fbx#2이다. 대표 에셋은 접미사를 쓰지 않으므로
    /// 접미사 없는 경로 참조도 같은 대표 에셋을 가리킨다.
    /// </summary>
    [[nodiscard]] std::string ToString() const;

    /// <summary>
    /// 장면 파일 형태를 읽는다. 접미사 없는 텍스트는 대표 에셋을 가리키며, 지금까지 쓰인 모든
    /// 참조가 여전히 유효한 이유가 그것이다. 숫자가 아닌 접미사는 경로의 일부다: 파일 이름에는
    /// 정당하게 '#'이 들어갈 수 있다.
    /// </summary>
    [[nodiscard]] static AssetReference Parse(std::string_view text);

    [[nodiscard]] bool operator==(const AssetReference&) const = default;

private:
    std::filesystem::path mPath;
    Core::Guid mGuid;
    std::uint32_t mLocalId = MainAssetLocalId;
};

}
