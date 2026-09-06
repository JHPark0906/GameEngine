#include "Rules/EditorConfirmation.h"

#include <utility>

namespace GameEditor
{

void ConfirmationQueue::Ask(ConfirmationRequest request)
{
    mPending.push_back(std::move(request));
}

const ConfirmationRequest* ConfirmationQueue::GetCurrent() const
{
    return mPending.empty() ? nullptr : &mPending.front();
}

void ConfirmationQueue::Answer(const std::optional<std::size_t> chosen)
{
    if (mPending.empty())
    {
        return;
    }

    // 답을 부르기 전에 줄에서 뺀다. 답을 받은 쪽이 그 자리에서 다시 물을 수 있고 — "지울까요"
    // 뒤에 "정말로?"가 오는 식이다 — 그 Ask가 이 벡터에 밀어 넣으면 담는 자리가 옮겨간다.
    // 그때 앞자리를 가리키고 있던 참조는 이미 사라진 자리를 가리킨다. 순서가 아니라 수명의
    // 문제이므로 겉으로 드러나는 차이는 없다: 시험은 이것을 잡지 못한다.
    const ConfirmationRequest answered = std::move(mPending.front());
    mPending.erase(mPending.begin());
    if (answered.onAnswered)
    {
        answered.onAnswered(chosen);
    }
}

}
