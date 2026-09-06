#pragma once

// editor-layer: 0 (Rules)

#include <filesystem>
#include <optional>

#include "Platform/PlatformServices.h"

namespace GameEditor
{

/// <summary>
/// 사람에게 파일 경로를 묻는 길이다. 요청과 답의 모양은 플랫폼 계약 그대로다 —
/// <see cref="GameEngine::Platform::PlatformServices::FileDialogRequest"/>를 받고, 고른 경로를
/// 돌려주며, 취소는 빈 optional이다.
///
/// 파일 대화상자는 사람의 입력을 기다리므로 인터페이스로 주입한다.
/// 테스트가 창 조작 없이 프로젝트 열기, 장면 변경, 스크립트 생성 경로를 검증할 수 있다.
///
/// 확장자를 채우는 일은 <b>여기가 아니라 구현이 한다</b>. Win32 공용 대화상자가 요청의 기본
/// 확장자로 그것을 하고, 편집기 쪽 코드는 돌려받은 경로를 그대로 쓴다.
/// </summary>
class IFileDialogs
{
public:
    virtual ~IFileDialogs() = default;

    /// <summary>열 파일을 고르게 한다. 취소하면 빈 optional이다.</summary>
    [[nodiscard]] virtual std::optional<std::filesystem::path> ShowOpen(
        const GameEngine::Platform::PlatformServices::FileDialogRequest& request) = 0;

    /// <summary>저장할 파일을 고르게 한다. 덮어쓰기 확인을 포함한다.</summary>
    [[nodiscard]] virtual std::optional<std::filesystem::path> ShowSave(
        const GameEngine::Platform::PlatformServices::FileDialogRequest& request) = 0;
};

/// <summary>플랫폼의 공용 대화상자에 그대로 넘기는 구현이다. 편집기가 실제로 쓰는 것이다.</summary>
class PlatformFileDialogs final : public IFileDialogs
{
public:
    [[nodiscard]] std::optional<std::filesystem::path> ShowOpen(
        const GameEngine::Platform::PlatformServices::FileDialogRequest& request) override;

    [[nodiscard]] std::optional<std::filesystem::path> ShowSave(
        const GameEngine::Platform::PlatformServices::FileDialogRequest& request) override;
};

}
