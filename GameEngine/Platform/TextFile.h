#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace GameEngine::Platform
{

/// <summary>
/// 파일 하나를 통째로 읽는다. 열지 못했거나 읽다 실패하면 값이 없다.
///
/// 바이너리로 읽는다. 텍스트 모드는 Windows에서 줄 끝을 바꿔 버리므로, 해시를 재거나 바이트를
/// 그대로 비교하는 자리에서 파일과 읽은 것이 달라진다.
///
/// <b>배포된 콘텐츠를 읽는 길이 아니다.</b> 그것은 <c>IContentSource</c>가 답하며, 패키징된
/// 게임에서는 파일이 실행 파일 안에 있을 수도 있다. 이 함수가 답하는 것은 「디스크의 이 경로」가
/// 확실한 자리 — 설정, 프로젝트 서술자, 사람이 편집하는 파일 — 뿐이다.
/// </summary>
[[nodiscard]] std::optional<std::string> ReadTextFile(const std::filesystem::path& path);

/// <summary>파일을 쓰지 못한 이유다. 부르는 쪽이 사람에게 무엇을 말할지 여기서 갈린다.</summary>
enum class FileWriteError : unsigned char
{
    None,
    /// <summary>임시 파일 자리에 이미 무엇이 있다. 덮으면 남의 작업 중인 쓰기를 깬다.</summary>
    TemporaryFileInTheWay,
    /// <summary>열지 못했거나 쓰다 실패했다. 목적지는 손대지 않았다.</summary>
    WriteFailed,
    /// <summary>다 썼는데 제자리로 옮기지 못했다. 목적지는 예전 내용 그대로다.</summary>
    ReplaceFailed,
};

/// <summary>쓰기의 결과다. 성공이면 <c>bool</c>로 참이고, 아니면 왜인지를 담는다.</summary>
struct FileWriteResult
{
    FileWriteError error = FileWriteError::None;
    /// <summary>파일시스템이 준 이유다. 있으면 메시지에 그대로 실어 준다.</summary>
    std::error_code systemError;

    [[nodiscard]] explicit operator bool() const { return error == FileWriteError::None; }
    /// <summary>사람에게 보일 한 마디다. 무엇이 남아 있는지까지 말한다.</summary>
    [[nodiscard]] std::string_view Describe() const;
};

/// <summary>파일에 텍스트를 쓴다. 있던 내용은 사라진다.</summary>
[[nodiscard]] FileWriteResult WriteTextFile(
    const std::filesystem::path& path, std::string_view text);

/// <summary>
/// 임시 파일에 다 쓰고 나서 목적지 위로 옮긴다.
///
/// 목적지에 바로 쓰다 중간에 실패하면 — 디스크가 차거나 프로세스가 죽으면 — 잘린 파일만 남고
/// 원래 내용은 이미 사라진 뒤다. 사용자의 작업물이 이 길로 파일이 되는 자리에서는, 실패가
/// <b>파손</b>이 아니라 <b>무산</b>으로 끝나야 한다.
///
/// 이미 있는 임시 파일은 덮는다. 죽은 쓰기가 남긴 그것 하나 때문에 저장이 영원히 막히는 쪽이
/// 더 나쁘기 때문이다. 그 자리를 지켜야 하는 부르는 쪽은 — 두 파일을 함께 바꾸는 곳처럼 — 부르기
/// 전에 스스로 묻는다. 그 검사는 여러 파일을 한꺼번에 보아야 하므로 이 함수 안에 들어올 수 없다.
/// </summary>
[[nodiscard]] FileWriteResult WriteTextFileAtomically(
    const std::filesystem::path& path, std::string_view text);

}
