#pragma once

// editor-layer: 2 (Views)

#include <filesystem>

#include "Rules/EditorConfirmation.h"

namespace GameEditor
{

class EditorContext;

/// <summary>
/// 방금 연 프로젝트가 남긴 복구 사본이 있으면 하나씩 묻고, 답대로 되살리거나 지운다.
/// 주인이 사라진 사본이 있으면 날짜와 함께 보여 주고 지울 기회를 준다.
///
/// 물음은 에디터 자신의 창으로 서므로 프레임 안에서 불러도 된다 — 사람을 기다리는 동안에도
/// 프레임은 계속 돈다. 그래서 그 물음이 떠 있는 동안 창을 닫으려 해도 굳지 않는다.
/// 규칙 자체는 <c>EditorRecovery</c>에 있고 창을 모르므로 창 없이 시험한다.
/// </summary>
/// <param name="context">방금 프로젝트를 연 문서 모델이다.</param>
/// <param name="queue">물음이 설 줄이다.</param>
/// <param name="recoveryDirectory">
/// 사본이 사는 자리다. 편집기는 <c>GetRecoveryDirectory()</c>를 주고, 시험은 자기 프로세스의
/// 임시 자리를 준다. 인자인 이유는 이 함수가 그 디렉터리를 훑고 답에 따라 지우기 때문이다 —
/// 자리를 함수가 스스로 정하면 시험이 사용자의 진짜 사본을 만지게 되고, 그것은 지우는 답변이
/// 한 줄 더해지는 날 사용자의 작업을 잃는 모양이 된다.
/// </param>
void AskAboutRecoverySnapshots(
    EditorContext& context, ConfirmationQueue& queue,
    const std::filesystem::path& recoveryDirectory);

}
