#pragma once

#include <cstddef>
#include <optional>

#include "../PlatformServices.h"

namespace GameEngine::Platform::Win32
{

/// <summary>
/// 버튼이 여럿인 모달 대화상자를 띄우고, 눌린 선택지의 인덱스를 답한다. 취소 버튼, 닫기 상자,
/// Esc는 모두 결과 없음이고, 대화상자를 만들지 못한 실패는 로그로 말한다.
/// </summary>
[[nodiscard]] std::optional<std::size_t> ShowChoiceDialog(
    const PlatformServices::ChoiceDialogRequest& request);

/// <summary>
/// 대화상자가 떠 있는 동안 사람이 창을 닫으려 했음을 알린다. 떠 있는 것은 취소로 끝내고,
/// 그 뒤에 서려던 물음들도 묻지 않고 취소로 답한다.
///
/// 이 길이 필요한 이유는 자리다. 시작하며 뜨는 물음은 메시지 루프가 돌기 전에 서므로, 그
/// 동안 도착한 <c>WM_CLOSE</c>를 꺼내 처리할 루프가 없다 — 사람에게는 창이 굳은 것으로
/// 보인다. 그때 닫으려는 사람의 뜻은 "지금은 답하지 않겠다"이므로 취소가 곧 그 답이다.
/// </summary>
void RequestChoiceDialogCancel();

/// <summary>
/// 닫기 요청이 응용에 전해졌으니 위의 표시를 내린다. 표시를 남겨 두면 닫기를 그만둔 뒤의
/// 물음들까지 소리 없이 취소된다.
/// </summary>
void ClearChoiceDialogCancel();

}
