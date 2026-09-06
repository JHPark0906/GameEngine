#pragma once

#include <functional>
#include <optional>
#include <string_view>

#include "IProcessRunner.h"

namespace GameEngine::Platform
{

/// <summary>
/// 프로그램 하나를 끝날 때까지 돌리고 종료 코드를 돌려준다. 시작하지 못했으면 비어 있다.
///
/// 막는다. 그래도 되는 자리에만 쓴다 — 명령줄 도구가 그렇다: 사람이 결과를 기다리고 있고,
/// 그동안 그려야 할 화면이 없다. 에디터는 이것을 부르지 않고 <see cref="IProcessRunner"/>를
/// 직접 쥔다.
///
/// 막는 동안에도 줄은 나오는 대로 넘어간다. 다 끝난 뒤에 한꺼번에 주면 몇 분짜리 빌드가
/// 아무 말 없이 멈춰 있는 것처럼 보이고, 사람은 그것을 멎었다고 읽는다.
/// </summary>
/// <param name="request">돌릴 프로그램이다.</param>
/// <param name="onLine">줄 하나를 받는 호출 가능 객체다. 줄바꿈은 이미 떼어져 있다.</param>
[[nodiscard]] std::optional<int> RunProcessToCompletion(
    const ProcessRequest& request, const std::function<void(std::string_view line)>& onLine);

}
