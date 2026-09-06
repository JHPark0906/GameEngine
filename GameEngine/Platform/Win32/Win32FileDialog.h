#pragma once

#include <filesystem>
#include <optional>

#include "../PlatformServices.h"

namespace GameEngine::Platform::Win32
{

/// <summary>공용 대화상자로 파일을 고른다. 취소는 결과 없음이고, 실패는 로그로 말한다.</summary>
[[nodiscard]] std::optional<std::filesystem::path> ShowFileDialog(
    const PlatformServices::FileDialogRequest& request, bool save);

}
