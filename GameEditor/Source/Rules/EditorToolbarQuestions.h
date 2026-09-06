#pragma once

// editor-layer: 0 (Rules)

#include <optional>
#include <string>

namespace GameEditor
{

/// <summary>
/// 툴바가 사람에게 묻는 것들의 글이다. 창도 버튼도 모르며 글만 만든다.
///
/// 글을 그리는 곳에서 떼어 내는 이유는 두 가지다. 하나는 시험이다 — 「무엇을 지우는지 이름으로
/// 적는가」는 창 없이 잴 수 있어야 한다. 다른 하나는 이 글들이 사람의 파일과 되돌릴 수 없는
/// 동작을 다루기 때문이다: 무엇에 답하는지 모르는 물음은 답을 받아도 답이 아니다.
/// </summary>

/// <summary>
/// 저장되지 않은 편집을 두고 장면을 떠나려 할 때 묻는 글이다.
/// </summary>
[[nodiscard]] std::string MakeUnsavedChangesQuestion();

/// <summary>
/// 장면을 지울지 묻는 글이다. 이름을 함께 적는 이유는 이것이 이 에디터에서 사람의 파일을
/// 없애는 유일한 자리이기 때문이다 — "정말 지울까"만으로는 무엇에 답하는지 알 수 없다.
/// </summary>
/// <param name="scenePath">지울 장면의 프로젝트 안 경로다. 없으면 이름 없이 묻는다.</param>
/// <returns>사람에게 보일 글이다.</returns>
[[nodiscard]] std::string MakeDeleteSceneQuestion(
    const std::optional<std::string>& scenePath);

}
