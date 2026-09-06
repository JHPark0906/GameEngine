#include "Document/EditorUndoService.h"

#include <utility>

namespace GameEditor
{

unsigned int EditorUndoService::ResolveObjectId(const unsigned int instanceId) const
{
    const auto alias = mAliases.find(instanceId);
    if (alias == mAliases.end())
    {
        return instanceId;
    }
    // 재귀가 사슬 길이만큼 내려갔다가, 돌아오며 지나간 마디를 전부 끝으로 눌러 둔다. id는
    // 재사용되지 않으므로(레지스트리가 단조 증가) 사슬에 순환은 없다.
    const unsigned int resolved = ResolveObjectId(alias->second);
    alias->second = resolved;
    return resolved;
}

void EditorUndoService::RecordObjectIdAlias(const unsigned int oldId, const unsigned int newId)
{
    const unsigned int from = ResolveObjectId(oldId);
    if (from != newId)
    {
        mAliases[from] = newId;
    }
}

void EditorUndoService::Record(std::unique_ptr<GameEngine::Core::IEditCommand> command)
{
    mStack.Push(std::move(command));
}

void EditorUndoService::Reset()
{
    mStack.Clear();
    mAliases.clear();
}

}
