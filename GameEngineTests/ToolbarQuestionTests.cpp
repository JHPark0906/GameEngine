#include "ToolbarQuestionTests.h"

#include <iostream>
#include <optional>
#include <string>

#include <vector>

#include "Rules/EditorToolbarLayout.h"
#include "Rules/EditorToolbarQuestions.h"
#include "TestSupport.h"

namespace
{
    using TestSupport::Expect;
}

bool RunToolbarQuestionTests()
{
    std::cout << "running toolbar question tests\n";

    // 이름이 있으면 이름을 적는다. 이것이 이 에디터에서 사람의 파일을 없애는 유일한 자리다.
    const std::string named =
        GameEditor::MakeDeleteSceneQuestion(std::string("Scenes/Main.scene"));
    std::cout << "  with a name: \"" << named << "\"\n";
    bool passed = Expect(
        named.find("Scenes/Main.scene") != std::string::npos,
        "the delete question should name the scene it is about");
    passed = Expect(
        named.find("cannot be undone") != std::string::npos,
        "the delete question should say that it cannot be undone") && passed;

    // 이름을 모를 때도 되돌릴 수 없다는 것만은 말한다. 그것이 이 물음의 무게다.
    const std::string unnamed = GameEditor::MakeDeleteSceneQuestion(std::nullopt);
    std::cout << "  without a name: \"" << unnamed << "\"\n";
    passed = Expect(
        unnamed.find("cannot be undone") != std::string::npos,
        "a delete question with no name should still say it cannot be undone") && passed;
    passed = Expect(named != unnamed, "naming the scene should change the question") && passed;

    const std::string empty = GameEditor::MakeDeleteSceneQuestion(std::string());
    passed = Expect(
        empty == unnamed, "an empty name should read the same as no name at all") && passed;

    // 저장 물음은 무엇이 걸려 있는지 말한다. 버튼 라벨이 무엇을 하는지 말하므로 글은 상태를
    // 말하면 된다.
    const std::string unsaved = GameEditor::MakeUnsavedChangesQuestion();
    std::cout << "  unsaved: \"" << unsaved << "\"\n";
    passed = Expect(!unsaved.empty(), "the unsaved question should say something") && passed;
    passed = Expect(
        unsaved.find("unsaved") != std::string::npos,
        "the unsaved question should name what is at stake") && passed;

    return passed;
}

bool RunToolbarFoldAfterQuestionsMovedTests()
{
    std::cout << "running toolbar fold tests\n";

    GameEditor::ToolbarLayoutMetrics metrics;

    // 툴바 버튼들의 폭이다. 재는 것은 줄 수이므로 같은 대표 폭으로 계산한다.
    constexpr float ButtonWidth = 96.0f;
    const std::vector<float> afterMove(12, ButtonWidth);

    std::vector<float> beforeMove(12 + 9, ButtonWidth);

    constexpr float NarrowWindow = 900.0f;
    const GameEditor::ToolbarLayout after =
        GameEditor::ComputeToolbarLayout(afterMove, NarrowWindow, metrics);
    const GameEditor::ToolbarLayout before =
        GameEditor::ComputeToolbarLayout(beforeMove, NarrowWindow, metrics);

    std::cout << "  at " << NarrowWindow << " wide: " << before.rows << " row(s) with the"
              << " question buttons, " << after.rows << " without them\n";
    std::cout << "  strip height: " << before.height << " -> " << after.height << "\n";

    // 툴바에서 확인 버튼을 제외하면 줄 수와 높이가 늘지 않고 남은 모든 버튼의 사각형이 보존되어야 한다.
    bool passed = Expect(
        after.rows <= before.rows,
        "moving the questions out should not make the toolbar taller");
    passed = Expect(
        after.height <= before.height, "the strip should not grow once the questions leave") &&
        passed;
    // 그리고 사각형은 여전히 버튼마다 하나씩이다 — 접힘이 버튼을 잃지 않는다.
    passed = Expect(
        after.buttons.size() == afterMove.size(),
        "every button that stays should still get a place") && passed;

    return passed;
}

static const TestSupport::Registration gToolbarQuestionTests{
    "EditorDocument", "toolbar question tests should pass", RunToolbarQuestionTests };

static const TestSupport::Registration gToolbarFoldAfterQuestionsMovedTests{
    "EditorDocument", "toolbar fold tests should pass", RunToolbarFoldAfterQuestionsMovedTests };
