#pragma once

// editor-layer: 0 (Rules)

#include <filesystem>
#include <optional>

namespace GameEditor
{

/// <summary>
/// 프로젝트에 최소 머티리얼 파일 하나를 만든다: 텍스처 없음, 흰색 tint.
///
/// <see cref="CreateComponentScript"/>와 달리 놓일 수 있는 디렉터리를 가리지 않는다 — 컴포넌트는
/// 빌드 스크립트가 <c>Source/</c> 하나만 훑어서 다른 자리에 놓으면 어떤 빌드에도 안 들어가지만,
/// 머티리얼은 콘텐츠 소스를 통째로 훑는 <c>AssetDatabase::Refresh</c>가 찾으므로 프로젝트 안
/// 어디든 상관없다. 같은 이유로 다시 열 것도, 등록할 목록도 없다 — 다음 refresh가 그냥 찾는다.
/// </summary>
/// <param name="chosenFilePath">사람이 고른 자리다. 이미 파일이 있으면 만들지 않는다.</param>
/// <returns>만든 파일의 자리다. 이미 있거나 쓰지 못했으면 비어 있다.</returns>
[[nodiscard]] std::optional<std::filesystem::path> CreateMaterialAsset(
    const std::filesystem::path& chosenFilePath);

}
