#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "../GameEngine/Core/UndoStack.h"

#include "UndoStackTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEngine::Core::IEditCommand;
    using GameEngine::Core::UndoStack;

    /// <summary>
    /// 문서 구실을 하는 정수 하나다. 커맨드가 before/after로 값을 오가므로, undo/redo 뒤의 값이
    /// 곧 스택이 무엇을 실행했는지의 증거다.
    /// </summary>
    struct FakeDocument
    {
        int value = 0;
        /// <summary>살아 있는 대상 id들이다. 여기 없는 id를 겨눈 커맨드는 실패한다.</summary>
        std::vector<std::uint64_t> liveIds;

        [[nodiscard]] bool IsLive(const std::uint64_t id) const
        {
            for (const std::uint64_t live : liveIds)
            {
                if (live == id)
                {
                    return true;
                }
            }
            return false;
        }
    };

    /// <summary>값을 before에서 after로 바꾸는 커맨드다. mergeKey가 같으면 흡수한다.</summary>
    class SetValueCommand final : public IEditCommand
    {
    public:
        SetValueCommand(
            FakeDocument& document, const std::uint64_t targetId, const int before, const int after,
            const std::uint64_t mergeKey = 0)
            : mDocument(&document), mTargetId(targetId), mBefore(before), mAfter(after),
              mMergeKey(mergeKey)
        {
        }

        [[nodiscard]] bool Apply() override
        {
            if (!mDocument->IsLive(mTargetId))
            {
                return false;
            }
            mDocument->value = mAfter;
            return true;
        }

        [[nodiscard]] bool Revert() override
        {
            if (!mDocument->IsLive(mTargetId))
            {
                return false;
            }
            mDocument->value = mBefore;
            return true;
        }

        [[nodiscard]] bool TryMerge(const IEditCommand& next) override
        {
            const auto* const other = dynamic_cast<const SetValueCommand*>(&next);
            if (!other || mMergeKey == 0 || other->mMergeKey != mMergeKey ||
                other->mTargetId != mTargetId)
            {
                return false;
            }
            mAfter = other->mAfter;
            return true;
        }

    private:
        FakeDocument* mDocument;
        std::uint64_t mTargetId;
        int mBefore;
        int mAfter;
        std::uint64_t mMergeKey;
    };

    /// <summary>push된 것이 undo로 되돌고 redo로 재실행되는 기본 왕복이다.</summary>
    bool RunRoundTripTests()
    {
        FakeDocument document;
        document.liveIds = { 1 };
        UndoStack stack;
        const bool emptyAtFirst = !stack.CanUndo() && !stack.CanRedo() && !stack.Undo() && !stack.Redo();

        document.value = 10;
        stack.Push(std::make_unique<SetValueCommand>(document, 1, 0, 10));
        document.value = 20;
        stack.Push(std::make_unique<SetValueCommand>(document, 1, 10, 20));

        const bool undoOnce = stack.Undo() && document.value == 10;
        const bool undoTwice = stack.Undo() && document.value == 0 && !stack.CanUndo();
        const bool redoBoth = stack.Redo() && document.value == 10 &&
            stack.Redo() && document.value == 20 && !stack.CanRedo();

        return Expect(emptyAtFirst, "an empty stack should refuse undo and redo") &&
            Expect(undoOnce, "undo should revert the newest edit first") &&
            Expect(undoTwice, "a second undo should reach the original value") &&
            Expect(redoBoth, "redo should replay the reverted edits in order");
    }

    /// <summary>undo 뒤의 새 편집은 redo를 무효화한다 — 역사가 갈라지면 옛 미래는 없다.</summary>
    bool RunRedoInvalidationTests()
    {
        FakeDocument document;
        document.liveIds = { 1 };
        UndoStack stack;

        document.value = 10;
        stack.Push(std::make_unique<SetValueCommand>(document, 1, 0, 10));
        const bool undone = stack.Undo() && stack.CanRedo();

        document.value = 99;
        stack.Push(std::make_unique<SetValueCommand>(document, 1, 0, 99));

        return Expect(undone, "the first undo should leave a redo entry") &&
            Expect(!stack.CanRedo(), "a new edit should clear the redo stack") &&
            Expect(stack.Undo() && document.value == 0, "undo should revert the branched edit");
    }

    /// <summary>같은 병합 키의 연속 편집은 한 단계다 — 타이핑 하나하나가 undo가 되지 않게.</summary>
    bool RunMergeTests()
    {
        FakeDocument document;
        document.liveIds = { 1 };
        UndoStack stack;

        // "123"을 치는 세 번의 적용. 키가 같으므로 한 커맨드로 흡수된다.
        document.value = 1;
        stack.Push(std::make_unique<SetValueCommand>(document, 1, 0, 1, 7));
        document.value = 12;
        stack.Push(std::make_unique<SetValueCommand>(document, 1, 1, 12, 7));
        document.value = 123;
        stack.Push(std::make_unique<SetValueCommand>(document, 1, 12, 123, 7));
        const bool merged = stack.GetUndoCount() == 1;
        const bool undoneToOriginal = stack.Undo() && document.value == 0;
        const bool redoneToFinal = stack.Redo() && document.value == 123;

        // 키가 다르면 — 포커스를 잃었다 얻으면 — 새 단계다. 키 0은 아예 병합하지 않는다.
        document.value = 5;
        stack.Push(std::make_unique<SetValueCommand>(document, 1, 123, 5, 8));
        document.value = 6;
        stack.Push(std::make_unique<SetValueCommand>(document, 1, 5, 6, 0));
        document.value = 7;
        stack.Push(std::make_unique<SetValueCommand>(document, 1, 6, 7, 0));
        const bool distinctKeysStay = stack.GetUndoCount() == 4;

        // 병합도 새 편집이다: undo가 남긴 redo는 병합되는 push에도 지워진다.
        FakeDocument branched;
        branched.liveIds = { 1 };
        UndoStack branchedStack;
        branched.value = 1;
        branchedStack.Push(std::make_unique<SetValueCommand>(branched, 1, 0, 1, 7));
        branched.value = 2;
        branchedStack.Push(std::make_unique<SetValueCommand>(branched, 1, 1, 2, 0));
        const bool redoReady = branchedStack.Undo() && branchedStack.CanRedo();
        branched.value = 3;
        branchedStack.Push(std::make_unique<SetValueCommand>(branched, 1, 1, 3, 7));
        const bool mergedIntoTop = branchedStack.GetUndoCount() == 1;
        const bool redoClearedByMerge = !branchedStack.CanRedo();

        return Expect(merged, "edits sharing a merge key should collapse into one command") &&
            Expect(undoneToOriginal, "undoing the merged command should restore the first before") &&
            Expect(redoneToFinal, "redoing the merged command should restore the last after") &&
            Expect(distinctKeysStay, "distinct or zero merge keys should not merge") &&
            Expect(redoReady, "the branch setup should leave a redo entry") &&
            Expect(mergedIntoTop, "a push after undo should still merge with the new top") &&
            Expect(redoClearedByMerge, "a merged push should clear the redo stack like any edit");
    }

    /// <summary>용량을 넘으면 가장 오래된 편집부터 잊는다.</summary>
    bool RunCapacityTests()
    {
        FakeDocument document;
        document.liveIds = { 1 };
        UndoStack stack(2);

        for (int edit = 1; edit <= 3; ++edit)
        {
            document.value = edit;
            stack.Push(std::make_unique<SetValueCommand>(document, 1, edit - 1, edit));
        }
        const bool capped = stack.GetUndoCount() == 2;
        const bool undoAll = stack.Undo() && stack.Undo() && !stack.CanUndo();

        return Expect(stack.GetCapacity() == 2, "the stack should keep its configured capacity") &&
            Expect(capped, "pushing beyond capacity should drop the oldest command") &&
            Expect(undoAll && document.value == 1,
                "undoing everything should stop at the evicted edit's after value");
    }

    /// <summary>대상을 잃은 커맨드는 실패하고 버려진다 — 스택 꼭대기를 막지 않는다.</summary>
    bool RunFailureTests()
    {
        FakeDocument document;
        document.liveIds = { 1, 2 };
        UndoStack stack;

        document.value = 10;
        stack.Push(std::make_unique<SetValueCommand>(document, 1, 0, 10));
        document.value = 20;
        stack.Push(std::make_unique<SetValueCommand>(document, 2, 10, 20));

        // 대상 2가 사라졌다. 그 커맨드의 undo는 실패하고, 스택은 커맨드를 버려 다음 undo가
        // 그 아래의 멀쩡한 커맨드에 닿는다.
        document.liveIds = { 1 };
        const bool failed = !stack.Undo() && document.value == 20;
        const bool discarded = !stack.CanRedo();
        const bool nextUndoWorks = stack.Undo() && document.value == 0;

        return Expect(failed, "undo should fail when the command's target is gone") &&
            Expect(discarded, "a failed undo should discard the command, not move it") &&
            Expect(nextUndoWorks, "the next undo should reach the command underneath");
    }

    /// <summary>Clear는 양쪽을 비운다.</summary>
    bool RunClearTests()
    {
        FakeDocument document;
        document.liveIds = { 1 };
        UndoStack stack;

        document.value = 10;
        stack.Push(std::make_unique<SetValueCommand>(document, 1, 0, 10));
        document.value = 20;
        stack.Push(std::make_unique<SetValueCommand>(document, 1, 10, 20));
        const bool undone = stack.Undo();
        stack.Clear();

        return Expect(undone, "the setup undo should succeed") &&
            Expect(!stack.CanUndo() && !stack.CanRedo(), "clear should empty both stacks") &&
            Expect(stack.GetUndoCount() == 0 && stack.GetRedoCount() == 0,
                "clear should leave zero commands");
    }
}

bool RunUndoStackTests()
{
    return RunRoundTripTests() && RunRedoInvalidationTests() && RunMergeTests() &&
        RunCapacityTests() && RunFailureTests() && RunClearTests();
}

static const TestSupport::Registration gUndoStackTests{
    "Core", "undo stack tests should pass", RunUndoStackTests };
