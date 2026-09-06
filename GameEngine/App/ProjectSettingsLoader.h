#pragma once

#include <cstddef>
#include <span>

#include <filesystem>
#include <optional>

#include "ProjectSettings.h"

namespace GameEngine::App
{

/// <summary>배포된 설정 파일을 읽어 프로젝트 설정 객체로 변환한다.</summary>
class ProjectSettingsLoader final
{
public:
    /// <summary>지정한 JSON 파일에서 프로젝트 설정을 읽고 검증한다.</summary>
    /// <param name="filePath">읽을 프로젝트 설정 파일의 경로이다.</param>
    /// <returns>설정이 유효하면 ProjectSettings를, 실패하면 std::nullopt를 반환한다.</returns>
    [[nodiscard]] static std::optional<ProjectSettings> Load(
        std::span<const std::byte> fileBytes,
        const std::filesystem::path& filePath);
};

}
