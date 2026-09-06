#pragma once

// editor-layer: 0 (Rules)

#include <string>

namespace GameEditor
{

/// <summary>
/// 저장되지 않은 편집을 두고 다른 장면을 열려 할 때 시도가 어떻게 끝났는지다.
///
/// 갈라 두는 이유는 사람이 다음에 할 일이 다르기 때문이다. 저장이 실패했으면 다시 저장해야
/// 하고, 저장은 됐는데 열지 못했으면 다시 저장하면 안 된다 — 구별되지 않으면 사람은 저장을
/// 두 번 시도한다.
/// </summary>
enum class SceneSwitchFailure
{
    /// <summary>저장이 실패했다. 장면은 그대로이고 편집도 그대로다.</summary>
    SaveFailed,
    /// <summary>저장은 됐는데 그 장면을 열지 못했다.</summary>
    OpenFailedAfterSave,
    /// <summary>버리기를 골랐는데 열지 못했다. 편집은 아직 살아 있다.</summary>
    OpenFailedAfterDiscard,
};

/// <summary>
/// 다른 장면을 열지 묻는 글이다. 창도 버튼도 모르며 글만 만든다.
/// </summary>
[[nodiscard]] std::string MakeSceneSwitchQuestion();

/// <summary>
/// 위의 물음에 실패 한 줄을 붙인 글이다. 실패해도 물음을 닫지 않는 이유가 이 글이다 —
/// 물음이 사라지는 것을 사람은 "됐다"로 읽으므로, 실패한 채 사라지면 편집을 잃은 줄 모르고
/// 잃는다.
/// </summary>
/// <param name="failure">시도가 어떻게 끝났는지다.</param>
/// <returns>사람에게 보일 글이다.</returns>
[[nodiscard]] std::string MakeSceneSwitchQuestion(SceneSwitchFailure failure);

}
