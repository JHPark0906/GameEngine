#pragma once

// editor-layer: 0 (Rules)


#include <string>

namespace GameEditor
{

/// <summary>종료를 두고 사람이 고른 답이다.</summary>
enum class UnsavedChoice
{
    /// <summary>저장하고 닫는다.</summary>
    Save,
    /// <summary>저장하지 않고 닫는다.</summary>
    Discard,
    /// <summary>닫지 않는다.</summary>
    Cancel,
};

/// <summary>
/// 종료를 두고 물을 글이다. 저장하지 않고 닫으면 무엇이 남는지까지 말한다 — 그것이 이
/// 물음에서 사람이 알고 싶은 전부이고, 그 답이 갈리는 이유는 사본 쓰기가 실패할 수 있기
/// 때문이다. 그래서 사본을 이미 써 본 뒤에 이 글을 만든다: 약속이 아니라 사실을 적는다.
/// </summary>
/// <param name="copyWasKept">사본이 실제로 남아 있으면 true다.</param>
/// <returns>사람에게 보일 글이다.</returns>
[[nodiscard]] std::string MakeCloseQuestion(bool copyWasKept);

/// <summary>닫기 요청 하나를 두고 정해지는 것이다.</summary>
struct CloseDecision
{
    /// <summary>지금 곧바로 닫아도 되는지다.</summary>
    bool mayCloseNow = false;
    /// <summary>사람에게 물어야 하는지다.</summary>
    bool shouldAsk = false;
};

/// <summary>
/// 닫기 요청이 왔을 때 무엇을 할지 정한다.
///
/// 저장할 것이 없으면 곧바로 닫는다. 있으면 묻고, 그 답이 올 때까지 닫지 않는다 — 묻는 창은
/// 프레임이 돌아야 그려지므로 이 함수 안에서 답을 기다릴 수 없다. 「지금은 닫지 마라」를
/// 돌려주고 답이 온 뒤에 닫기를 청하는 것이 그래서다.
///
/// 이미 묻고 있으면 다시 묻지 않는다. 닫기를 연타하면 같은 물음이 쌓이고, 사람은 답한 만큼
/// 다시 답해야 한다.
/// </summary>
/// <param name="hasUnsavedChanges">저장되지 않은 편집이 있으면 true다.</param>
/// <param name="alreadyAsking">이미 닫기를 묻고 있으면 true다.</param>
/// <returns>이번 요청의 결정이다.</returns>
[[nodiscard]] CloseDecision DecideOnCloseRequest(bool hasUnsavedChanges, bool alreadyAsking);

/// <summary>
/// 물음이 끝난 뒤 사본을 남길지 정한다. 사본은 그것이 유일한 되돌릴 길일 때만 남는다.
///
/// 저장하지 않고 닫았으면 남긴다. 저장을 골랐는데 실패해서 창이 남았어도 남긴다 — 쓰지
/// 못하는 무언가가 있다는 뜻이므로 그물을 걷지 않는다. 그만두었거나 저장에 성공했으면
/// 지운다: 그 사본은 다음 열기에서 헛된 물음이 될 뿐이다.
/// </summary>
/// <param name="choice">사람이 고른 답이다.</param>
/// <param name="closing">에디터가 실제로 닫히면 true다.</param>
/// <returns>사본을 남겨야 하면 true다.</returns>
[[nodiscard]] bool ShouldKeepTheRecoveryCopy(UnsavedChoice choice, bool closing);

}
