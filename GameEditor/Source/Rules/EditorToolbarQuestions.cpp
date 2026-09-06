#include "Rules/EditorToolbarQuestions.h"

namespace GameEditor
{

std::string MakeUnsavedChangesQuestion()
{
    return "This scene has unsaved changes.";
}

std::string MakeDeleteSceneQuestion(const std::optional<std::string>& scenePath)
{
    if (!scenePath || scenePath->empty())
    {
        // 이름을 모를 때도 되돌릴 수 없다는 것만은 말한다. 그것이 이 물음의 무게다.
        return "Delete this scene? This cannot be undone.";
    }
    return "Delete " + *scenePath + "? This cannot be undone.";
}

}
