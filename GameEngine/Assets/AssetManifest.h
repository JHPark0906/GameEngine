#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Asset.h"
#include "../Core/Json.h"

namespace GameEngine::Assets
{

/// <summary>
/// 매니페스트가 에셋 하나에 대해 적어 둔 것이다. <b>표의 항목이 아니라 파일의 한 줄</b>이라,
/// 여기에는 색인도 포인터도 없고 파일에서 읽어낸 값만 있다.
/// </summary>
struct ManifestRecord
{
    Core::Guid guid;
    AssetType type = AssetType::ProjectSettings;
    std::filesystem::path relativePath;
    std::uint64_t contentHash = 0;
    std::uintmax_t fileSize = 0;
    std::vector<SubAsset> subAssets;

    /// <summary>
    /// 그 에셋의 항목 전체다. 형식만의 값 — 스프라이트의 픽셀 배율 같은 것 — 은 그 형식이
    /// 다시 읽으므로, 매니페스트는 무엇이 실려 있는지 모른 채 그것을 그대로 나른다.
    /// </summary>
    Core::Json entry;
};

/// <summary>
/// 표를 매니페스트 본문으로 적는다. <b>상태가 없다</b>: 에셋들을 받아 문자열을 답할 뿐이고,
/// 파일에 쓰는 일도 언제 쓸지 정하는 일도 부르는 쪽의 것이다.
///
/// 바이트가 실행마다 같아야 한다 — 빌드 산출물이고, 달라지면 그것을 읽는 쪽이 모든 에셋을
/// "바뀌었다"로 본다. 그래서 형식만의 항목은 이름순으로 적는다(Object에는 순서가 없다).
/// </summary>
/// <param name="assets">표의 에셋들이다. 적히는 순서가 곧 이 순서다.</param>
[[nodiscard]] std::string WriteManifestText(
    std::span<const std::unique_ptr<Asset>> assets);

/// <summary>
/// 매니페스트 본문을 레코드로 읽는다. 판본이 다르거나 항목이 깨졌으면 값이 없다.
///
/// 에셋을 만들지도 표에 넣지도 않는다 — 그것은 표가 할 일이고, 여기가 아는 것은 형식뿐이다.
/// </summary>
/// <param name="bytes">매니페스트 파일의 바이트다.</param>
/// <returns>읽어낸 레코드들이며, 읽을 수 없으면 빈 값이다.</returns>
[[nodiscard]] std::optional<std::vector<ManifestRecord>> ReadManifestText(
    std::span<const std::byte> bytes);

}
