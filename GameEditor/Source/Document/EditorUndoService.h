#pragma once

// editor-layer: 1 (Document)

#include <memory>
#include <unordered_map>

#include "Core/UndoStack.h"

namespace GameEditor
{

/// <summary>
/// 되돌리기 이력과, 커맨드가 잡아 둔 객체 id를 지금의 id로 잇는 별칭 표다.
///
/// 둘이 한 부품인 이유는 <b>수명이 하나이기 때문</b>이다. 커맨드는 대상을 인스턴스 id로
/// 잡아 두는데, 되돌리기가 객체를 지웠다 다시 만들면 그 객체는 <b>새 id</b>로 돌아온다.
/// 그래서 「옛 id → 새 id」를 적어 두지 않으면 그다음 되돌리기·다시하기가 이미 없는 id를
/// 가리키고 조용히 아무 일도 하지 않는다. 표가 필요한 것은 스택에 커맨드가 있는 동안뿐이고,
/// 스택이 비는 순간 표도 뜻을 잃는다 — 그래서 비우는 것도 함께다.
///
/// 별칭 해석은 사슬을 끝까지 따라간다. 한 객체가 여러 번 되살아나면 id가 여러 번 바뀌므로
/// 사슬이 길어지고, 중간 마디를 가리키는 커맨드도 끝을 봐야 한다. 해석하면서 지나간 마디를
/// 전부 끝으로 눌러 두므로 사슬은 자라기만 하고 깊어지지는 않는다. id는 재사용되지 않아
/// (레지스트리가 단조 증가) 순환은 생기지 않는다.
///
/// <b>편집을 기록할지 말지는 여기서 정하지 않는다.</b> 그 답은 열린 장면이 있는 Edit 모드인가
/// 하는 문서의 물음이고, 표시를 세우는 것도 문서의 일이다. 이 부품은 「기록해라」를 받으면
/// 쌓기만 한다.
/// </summary>
class EditorUndoService final
{
public:
    /// <summary>되돌리기 스택이다. 툴바와 단축키가 직접 민다.</summary>
    [[nodiscard]] GameEngine::Core::UndoStack& GetStack() { return mStack; }
    [[nodiscard]] const GameEngine::Core::UndoStack& GetStack() const { return mStack; }

    /// <summary>
    /// 커맨드가 잡아 둔 id를 지금 살아 있는 id로 바꾼다. 별칭이 없으면 그대로 돌려준다.
    /// </summary>
    [[nodiscard]] unsigned int ResolveObjectId(unsigned int instanceId) const;

    /// <summary>
    /// 옛 id가 이제 새 id라고 적는다. 옛 id가 이미 별칭을 갖고 있으면 사슬의 <b>끝</b>에
    /// 잇는다 — 중간에 이으면 먼저 적힌 별칭이 끊긴다.
    /// </summary>
    void RecordObjectIdAlias(unsigned int oldId, unsigned int newId);

    /// <summary>커맨드 하나를 쌓는다. 쌓아도 되는지는 부르는 쪽이 이미 보았다.</summary>
    void Record(std::unique_ptr<GameEngine::Core::IEditCommand> command);

    /// <summary>이력과 별칭을 함께 비운다. 편집 대상 문서가 바뀌는 자리가 부른다.</summary>
    void Reset();

private:
    GameEngine::Core::UndoStack mStack;
    /// <summary>
    /// 옛 인스턴스 id → 새 인스턴스 id. 경로 압축이 조회 중에 표를 다듬으므로 mutable이다 —
    /// 관찰 가능한 상태는 바뀌지 않는다.
    /// </summary>
    mutable std::unordered_map<unsigned int, unsigned int> mAliases;
};

}
