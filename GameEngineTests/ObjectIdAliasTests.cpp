#include <iostream>
#include <memory>

#include "../GameEditor/Source/Document/EditorUndoService.h"

#include "ObjectIdAliasTests.h"
#include "TestSupport.h"

using GameEditor::EditorUndoService;
using TestSupport::Expect;

namespace
{

/// <summary>아무것도 하지 않지만 쌓일 수는 있는 편집이다. 스택이 비는지만 보면 된다.</summary>
class NoOpEditCommand final : public GameEngine::Core::IEditCommand
{
public:
    [[nodiscard]] bool Apply() override { return true; }
    [[nodiscard]] bool Revert() override { return true; }
};

}

bool RunObjectIdAliasTests()
{
    std::cout << "running object id alias tests\n";
    bool passed = true;

    // 별칭이 없는 id는 그대로 지나간다. 대부분의 조회가 이 길이다.
    {
        const EditorUndoService undo;
        passed = Expect(
            undo.ResolveObjectId(7) == 7,
            "an id with no alias should resolve to itself") && passed;
    }

    // ---- 사슬을 끝까지 따라간다 ----
    //
    // 한 객체가 세 번 되살아나면 id가 세 번 바뀐다. 첫 id를 잡아 둔 커맨드도 마지막 id를
    // 봐야 한다 — 중간에서 멈추면 이미 없는 객체를 가리킨다.
    {
        EditorUndoService undo;
        undo.RecordObjectIdAlias(1, 2);
        undo.RecordObjectIdAlias(2, 3);
        undo.RecordObjectIdAlias(3, 4);
        passed = Expect(
            undo.ResolveObjectId(1) == 4,
            "an id should resolve through the whole chain, not one link") && passed;
        passed = Expect(
            undo.ResolveObjectId(2) == 4 && undo.ResolveObjectId(3) == 4,
            "every link in the chain should resolve to its end") && passed;
    }

    // ---- 이미 별칭이 있는 id에 이으면 사슬의 끝에 붙는다 ----
    //
    // 이것이 RecordObjectIdAlias가 옛 id를 먼저 해석하는 이유다. 옛 id를 그대로 키로 쓰면
    // 1→2를 1→9로 덮어써서, 2를 잡아 둔 커맨드가 9에 닿지 못한 채 남는다.
    {
        EditorUndoService undo;
        undo.RecordObjectIdAlias(1, 2);
        undo.RecordObjectIdAlias(1, 9);
        passed = Expect(
            undo.ResolveObjectId(1) == 9,
            "aliasing an already-aliased id should still resolve the original to the newest")
            && passed;
        passed = Expect(
            undo.ResolveObjectId(2) == 9,
            "the middle of the chain should reach the newest id too, not be cut off") && passed;
    }

    // 자기 자신으로의 별칭은 적지 않는다. 적으면 해석이 끝나지 않는다.
    {
        EditorUndoService undo;
        undo.RecordObjectIdAlias(5, 5);
        passed = Expect(
            undo.ResolveObjectId(5) == 5,
            "aliasing an id to itself should leave it resolvable") && passed;
    }

    // ---- 비우면 이력과 표가 함께 빈다 ----
    //
    // 표는 스택에 커맨드가 있는 동안에만 뜻이 있다. 편집 대상 문서가 바뀌면 둘 다 가리킬 것이
    // 없어지므로 하나만 비우면 남은 쪽이 거짓말을 한다.
    {
        EditorUndoService undo;
        undo.Record(std::make_unique<NoOpEditCommand>());
        undo.RecordObjectIdAlias(1, 2);
        passed = Expect(undo.GetStack().CanUndo(), "a recorded edit should be undoable") && passed;

        undo.Reset();
        passed = Expect(!undo.GetStack().CanUndo(), "resetting should empty the history") && passed;
        passed = Expect(
            undo.ResolveObjectId(1) == 1,
            "resetting should forget the aliases as well as the history") && passed;
    }

    return passed;
}

static const TestSupport::Registration gObjectIdAliasTests{
    "EditorDocument", "object id alias tests should pass", RunObjectIdAliasTests };
