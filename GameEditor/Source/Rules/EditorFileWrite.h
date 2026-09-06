#pragma once

// editor-layer: 0 (Rules)

#include <filesystem>
#include <string>

namespace GameEditor
{

/// <summary>
/// 파일을 원자적으로 쓰고, 실패하면 <b>어느 파일을 쓰다 왜 실패했는지</b> 로그로 말한다.
///
/// 임시 파일에 다 쓰고 나서 목적지 위로 교체하는 것이 <c>Core::WriteTextFileAtomically</c>이고,
/// 여기서 더하는 것은 그 실패를 사람이 읽을 한 문장으로 만드는 일뿐이다. 목적지에 직접 쓰다
/// 중간에 실패하면 — 디스크가 차거나 프로세스가 죽으면 — 잘린 파일만 남고 원본은 이미 사라진
/// 뒤다. 사용자의 작업물이 이 길로만 파일이 되므로, 실패가 파손 대신 무산으로 끝나야 한다.
///
/// <b>왜 Core가 아니라 여기인가.</b> 엔진의 Core는 열 파일 중 하나도 로그를 쓰지 않는다 —
/// 답을 돌려줄 뿐 말하지 않는 것이 그 층의 성질이고, <c>FileWriteResult</c>가 <c>Describe</c>를
/// 갖는 것도 부르는 쪽이 말하라는 뜻이다. 실패를 어떤 문장으로 알릴지는 편집기의 정책이므로
/// 편집기 쪽에 둔다.
/// </summary>
/// <param name="fullPath">최종 파일 경로다.</param>
/// <param name="text">기록할 내용이다.</param>
/// <returns>내용이 목적지에 놓였으면 true이다.</returns>
[[nodiscard]] bool WriteFileAtomically(
    const std::filesystem::path& fullPath, const std::string& text);

}
