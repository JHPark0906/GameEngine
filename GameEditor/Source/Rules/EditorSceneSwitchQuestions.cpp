#include "Rules/EditorSceneSwitchQuestions.h"

namespace GameEditor
{

std::string MakeSceneSwitchQuestion()
{
    return "This scene has unsaved changes. Open the other scene anyway?";
}

std::string MakeSceneSwitchQuestion(const SceneSwitchFailure failure)
{
    std::string question = MakeSceneSwitchQuestion();
    switch (failure)
    {
    case SceneSwitchFailure::SaveFailed:
        // 다시 저장해야 하는 경우다.
        question += "\nSaving failed, so nothing changed. See the Console.";
        break;
    case SceneSwitchFailure::OpenFailedAfterSave:
        // 다시 저장하면 안 되는 경우다. 그 사실을 말하지 않으면 사람은 저장을 또 누른다.
        question += "\nYour work was saved, but that scene could not be opened. See the Console.";
        break;
    case SceneSwitchFailure::OpenFailedAfterDiscard:
        // 버렸는데 열지 못했다. 편집이 아직 살아 있다는 것이 여기서 가장 중요한 사실이다.
        question += "\nThat scene could not be opened, so your edits are still here."
                    " See the Console.";
        break;
    }
    return question;
}

}
