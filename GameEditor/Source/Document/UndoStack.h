#pragma once

// editor-layer: 1 (Document)

#include <cstddef>
#include <memory>
#include <vector>

namespace GameEditor
{

/// <summary>
/// 되돌릴 수 있는 편집 하나다. Apply가 편집을 실행 — 재실행 포함 — 하고, Revert가 되돌린다.
///
/// 커맨드는 대상을 포인터가 아니라 인스턴스 id 같은 이름으로 잡아 두고 실행 시점에 해석해야
/// 한다: 대상은 스택에 쌓여 있는 동안 삭제되고 재생성될 수 있고, 그때 포인터는 남는 것이 없다.
/// 재생성이 이름을 바꾸는 문제 — 되살아난 객체가 새 id를 받는 것 — 는 스택의 일이 아니라
/// 해석기의 일이다: 에디터는 옛 id를 현재 id로 잇는 별칭 맵을 해석 앞에 두므로, 커맨드는 처음
/// 잡은 이름을 영원히 쥐고 있으면 된다. 해석에 실패하면 false를 반환한다 — 스택은 그 커맨드를
/// 옮기는 대신 버린다.
/// </summary>
class IEditCommand
{
public:
    virtual ~IEditCommand() = default;

    /// <summary>편집을 실행한다. 재실행(redo)도 이 길이다. 대상이 해석되지 않으면 false다.</summary>
    [[nodiscard]] virtual bool Apply() = 0;

    /// <summary>편집을 되돌린다. 대상이 해석되지 않으면 false다.</summary>
    [[nodiscard]] virtual bool Revert() = 0;

    /// <summary>
    /// 뒤이어 기록되려는 커맨드를 이 커맨드 안으로 흡수한다. 텍스트 필드의 연속 타이핑이 한 번의
    /// undo가 되는 길이다: 흡수한 쪽은 자기 "이전 값"을 지키고 "새 값"만 next의 것으로 바꾼다.
    /// 흡수했으면 true — next는 스택에 들어가는 대신 버려진다. 기본은 흡수하지 않는 것이다.
    /// </summary>
    [[nodiscard]] virtual bool TryMerge(const IEditCommand& next)
    {
        static_cast<void>(next);
        return false;
    }
};

/// <summary>
/// 편집 문서의 undo/redo 커맨드 스택이다. 커맨드가 무엇을 편집하는지 모르고,
/// Apply/Revert/TryMerge 계약으로 편집 이력을 관리한다.
///
/// Push는 "이미 실행된" 커맨드를 기록한다. 스택이 실행까지 맡지 않는 이유는, 에디터의 편집이
/// 이미 각자의 길 — 인스펙터의 setter, 계층 창의 드래그 — 로 실행된 뒤에 기록되기 때문이다.
///
/// 실패 정책: Undo/Redo 중 커맨드의 Revert/Apply가 false면 그 커맨드는 반대쪽 스택으로 옮겨지지
/// 않고 버려진다. 실패는 대상이 더는 해석되지 않는다는 뜻이고, 그 커맨드를 붙들고 있으면 같은
/// 실패가 스택 꼭대기를 막는다.
/// </summary>
class UndoStack final
{
public:
    static constexpr std::size_t DefaultCapacity = 128;

    explicit UndoStack(std::size_t capacity = DefaultCapacity);

    /// <summary>
    /// 이미 실행된 커맨드를 기록한다. 먼저 undo 꼭대기에 흡수(TryMerge)를 물어, 흡수되면
    /// 커맨드는 버려진다. 흡수됐든 아니든 새 편집은 redo 스택을 무효화하고 — 흡수도 역사를
    /// 가르는 새 편집이다 — 용량을 넘으면 가장 오래된 undo부터 버려진다.
    /// </summary>
    void Push(std::unique_ptr<IEditCommand> command);

    /// <summary>마지막 편집을 되돌린다. 되돌릴 것이 없거나 커맨드가 실패하면 false다.</summary>
    [[nodiscard]] bool Undo();

    /// <summary>마지막으로 되돌린 편집을 재실행한다. 재실행할 것이 없거나 실패하면 false다.</summary>
    [[nodiscard]] bool Redo();

    [[nodiscard]] bool CanUndo() const { return !mUndoCommands.empty(); }
    [[nodiscard]] bool CanRedo() const { return !mRedoCommands.empty(); }

    [[nodiscard]] std::size_t GetUndoCount() const { return mUndoCommands.size(); }
    [[nodiscard]] std::size_t GetRedoCount() const { return mRedoCommands.size(); }
    [[nodiscard]] std::size_t GetCapacity() const { return mCapacity; }

    /// <summary>양쪽 스택을 비운다. 편집 대상 문서가 통째로 바뀔 때 쓴다.</summary>
    void Clear();

private:
    std::vector<std::unique_ptr<IEditCommand>> mUndoCommands;
    std::vector<std::unique_ptr<IEditCommand>> mRedoCommands;
    std::size_t mCapacity;
};

}
