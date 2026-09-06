#pragma once

// editor-layer: 0 (Rules)

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace GameEditor
{

/// <summary>
/// 사람에게 물을 것 하나다. 창도 그리기도 모르며, 답은 콜백으로 온다.
///
/// 물음마다 버튼과 글이 다른 이유는 자리마다 다르기 때문이다: 장면을 지울지 묻는 줄과 참조를
/// 이관할지 묻는 줄은 같은 모양이지만 같은 말을 하지 않는다.
/// </summary>
struct ConfirmationRequest
{
    /// <summary>제목 줄이다. 비어 있으면 제목 없이 선다.</summary>
    std::string title;

    /// <summary>물음이다. 여러 줄일 수 있다.</summary>
    std::string question;

    /// <summary>버튼에 적힐 글들이다. 보이는 순서이고 첫 번째가 기본이다.</summary>
    std::vector<std::string> choices;

    /// <summary>취소 버튼의 글이다. 비어 있으면 취소 버튼을 두지 않는다.</summary>
    std::string cancelLabel;

    /// <summary>
    /// 지금은 고를 수 없는 선택지들의 첨자다. 보이되 눌리지 않는다.
    ///
    /// 감추지 않고 흐리게 두는 이유는, 감추면 사람이 그 길이 있다는 것 자체를 모르기
    /// 때문이다. 무엇이 막고 있는지는 물음의 글이 말한다.
    ///
    /// 물음은 뜨는 순간의 사진이라 이 목록도 그때 정해진다. 그동안 막힘이 풀릴 수 없는
    /// 이유는 창이 모달이어서다 — 답하기 전에는 그것을 풀 편집을 할 수 없다.
    /// </summary>
    std::vector<std::size_t> disabledChoices;

    /// <summary>
    /// 답이다. 고른 버튼의 첨자를 받고, 취소하거나 물음이 밖에서 닫히면 비어 있다.
    ///
    /// 비어 있는 것이 곧 "아무것도 하지 않음"이므로 부르는 쪽은 <c>if (chosen)</c> 하나로
    /// 다룰 수 있다. 답이 없는 것을 진행으로 읽으면 사람이 고르지 않은 일이 일어난다.
    /// </summary>
    std::function<void(std::optional<std::size_t> chosen)> onAnswered;

    /// <summary>다른 곳을 만지지 못하게 막을지다. 거짓이면 옆에서 계속 편집할 수 있다.</summary>
    bool modal = true;
};

/// <summary>
/// 물음들을 한 줄로 세우고, 한 번에 하나만 사람 앞에 둔다.
///
/// 줄을 세우는 이유는 겹침이다. 물음 둘이 함께 서면 사람은 자기가 어느 것에 답하는지 알 수
/// 없고, 답이 엉뚱한 동작에 붙는다. 그래서 앞의 것이 끝나야 다음이 선다.
///
/// 창을 모르므로 창 없이 시험할 수 있다. 그리는 쪽은 <c>GetCurrent</c>가 답하는 것을 세우고,
/// 사람이 고르면 <c>Answer</c>를 부른다.
/// </summary>
class ConfirmationQueue final
{
public:
    /// <summary>물음을 줄 끝에 세운다. 서 있는 것이 없으면 곧바로 선다.</summary>
    /// <param name="request">물을 것이다.</param>
    void Ask(ConfirmationRequest request);

    /// <summary>지금 사람 앞에 서 있는 물음이다. 없으면 null이다.</summary>
    [[nodiscard]] const ConfirmationRequest* GetCurrent() const;

    /// <summary>물음이 서 있는지다.</summary>
    [[nodiscard]] bool IsAsking() const { return GetCurrent() != nullptr; }

    /// <summary>줄에서 기다리는 것을 포함해 남은 물음의 수다.</summary>
    [[nodiscard]] std::size_t GetPendingCount() const { return mPending.size(); }

    /// <summary>
    /// 서 있는 물음에 답한다. 답은 정확히 한 번만 전해지고, 그 뒤 다음 물음이 선다.
    ///
    /// 한 번만인 것이 계약이다: 부르는 쪽이 "답이 두 번 올 수도 있다"를 다루지 않아도 되게,
    /// 그리고 되돌릴 수 없는 동작이 두 번 일어나지 않게.
    /// </summary>
    /// <param name="chosen">고른 버튼의 첨자다. 취소면 비어 있다.</param>
    void Answer(std::optional<std::size_t> chosen);

    /// <summary>
    /// 서 있는 물음을 취소로 끝낸다. 창이 밖에서 닫히는 길이며, 답은 비어 있는 것으로 간다.
    /// </summary>
    void CancelCurrent() { Answer(std::nullopt); }

private:
    std::vector<ConfirmationRequest> mPending;
};

}
