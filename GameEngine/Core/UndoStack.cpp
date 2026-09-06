#include "pch.h"
#include "UndoStack.h"

#include <utility>

namespace GameEngine::Core
{

UndoStack::UndoStack(const std::size_t capacity)
    : mCapacity(capacity > 0 ? capacity : 1)
{
}

void UndoStack::Push(std::unique_ptr<IEditCommand> command)
{
    if (!command)
    {
        return;
    }
    // 흡수돼도 새 편집은 새 편집이다: redo 스택은 어느 쪽이든 무효가 된다. 흡수가 redo를
    // 지키게 하는 것도 생각할 수 있으나, UI에서는 undo가 포커스를 거둬 병합 세션을 끊으므로
    // "redo가 남은 채로 흡수"는 사실상 일어나지 않는다 — 단순한 규칙 하나를 지킨다.
    mRedoCommands.clear();
    if (!mUndoCommands.empty() && mUndoCommands.back()->TryMerge(*command))
    {
        return;
    }
    mUndoCommands.push_back(std::move(command));
    if (mUndoCommands.size() > mCapacity)
    {
        mUndoCommands.erase(mUndoCommands.begin());
    }
}

bool UndoStack::Undo()
{
    if (mUndoCommands.empty())
    {
        return false;
    }
    // 실행 전에 꺼내 둔다: 커맨드가 Revert 중에 무엇을 하든 — 객체를 되살리고 별칭을 등록하는
    // 일 포함 — 실행 중인 자신이 스택 순회에 얽히지 않는다.
    std::unique_ptr<IEditCommand> command = std::move(mUndoCommands.back());
    mUndoCommands.pop_back();
    if (!command->Revert())
    {
        return false;
    }
    mRedoCommands.push_back(std::move(command));
    return true;
}

bool UndoStack::Redo()
{
    if (mRedoCommands.empty())
    {
        return false;
    }
    std::unique_ptr<IEditCommand> command = std::move(mRedoCommands.back());
    mRedoCommands.pop_back();
    if (!command->Apply())
    {
        return false;
    }
    mUndoCommands.push_back(std::move(command));
    return true;
}

void UndoStack::Clear()
{
    mUndoCommands.clear();
    mRedoCommands.clear();
}

}
