#include "SceneSwitchQuestionTests.h"

#include <iostream>
#include <string>

#include "Rules/EditorSceneSwitchQuestions.h"
#include "TestSupport.h"

namespace
{
    using TestSupport::Expect;
}

bool RunSceneSwitchQuestionTests()
{
    using GameEditor::MakeSceneSwitchQuestion;
    using GameEditor::SceneSwitchFailure;

    std::cout << "running scene switch question tests\n";

    const std::string asked = MakeSceneSwitchQuestion();
    const std::string saveFailed = MakeSceneSwitchQuestion(SceneSwitchFailure::SaveFailed);
    const std::string openFailedAfterSave =
        MakeSceneSwitchQuestion(SceneSwitchFailure::OpenFailedAfterSave);
    const std::string openFailedAfterDiscard =
        MakeSceneSwitchQuestion(SceneSwitchFailure::OpenFailedAfterDiscard);

    std::cout << "  asked: \"" << asked << "\"\n";

    // 처음 묻는 글은 무엇이 걸려 있는지 말한다.
    bool passed = Expect(
        asked.find("unsaved") != std::string::npos,
        "the question should say what is at stake");

    // 실패는 물음을 닫지 않고 붙는다. 그래서 셋 다 처음 물음을 품고 있어야 한다.
    for (const std::string& withFailure :
        { saveFailed, openFailedAfterSave, openFailedAfterDiscard })
    {
        passed = Expect(
            withFailure.starts_with(asked),
            "a failure should be added to the question, not replace it") && passed;
        passed = Expect(
            withFailure.size() > asked.size(), "a failure should say something") && passed;
    }

    // 셋이 서로 다르다. 같으면 사람은 무엇이 일어났는지 알 수 없다.
    passed = Expect(
        saveFailed != openFailedAfterSave && openFailedAfterSave != openFailedAfterDiscard &&
            saveFailed != openFailedAfterDiscard,
        "the three failures should read differently") && passed;

    // 저장이 실패했으면 다시 저장해야 한다 — 글이 아무것도 바뀌지 않았다고 말한다.
    passed = Expect(
        saveFailed.find("nothing changed") != std::string::npos,
        "a failed save should say that nothing changed") && passed;

    // 저장은 됐으면 다시 저장하면 안 된다 — 글이 저장됐다고 말해야 한다.
    std::cout << "  after a save that worked but an open that did not: \""
              << openFailedAfterSave << "\"\n";
    passed = Expect(
        openFailedAfterSave.find("saved") != std::string::npos,
        "a save that worked should be said so, or the person saves again") && passed;
    passed = Expect(
        openFailedAfterSave.find("nothing changed") == std::string::npos,
        "a save that worked should not read as if nothing changed") && passed;

    // 버렸는데 못 열었으면 편집이 아직 살아 있다는 것이 가장 중요한 사실이다.
    passed = Expect(
        openFailedAfterDiscard.find("still here") != std::string::npos,
        "edits that survived a failed open should be said to be still there") && passed;

    return passed;
}

static const TestSupport::Registration gSceneSwitchQuestionTests{
    "EditorDocument", "scene switch question tests should pass", RunSceneSwitchQuestionTests };
