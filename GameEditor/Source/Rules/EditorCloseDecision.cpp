#include "Rules/EditorCloseDecision.h"

namespace GameEditor
{

CloseDecision DecideOnCloseRequest(const bool hasUnsavedChanges, const bool alreadyAsking)
{
    CloseDecision decision;
    if (!hasUnsavedChanges)
    {
        decision.mayCloseNow = true;
        return decision;
    }
    // 물음이 이미 서 있으면 그것을 다시 세우지 않는다. 닫기를 연타해도 답할 것은 하나다.
    decision.shouldAsk = !alreadyAsking;
    return decision;
}

std::string MakeCloseQuestion(const bool copyWasKept)
{
    std::string question = "This scene has unsaved changes. Save them before closing?";
    if (copyWasKept)
    {
        question +=
            "\nA copy of the unsaved work has been kept, and the editor will offer to restore"
            "\nit the next time this project is opened.";
    }
    else
    {
        question +=
            "\nA copy could not be kept this time, so closing without saving loses the"
            "\nchanges. The console says why.";
    }
    return question;
}

bool ShouldKeepTheRecoveryCopy(const UnsavedChoice choice, const bool closing)
{
    switch (choice)
    {
    case UnsavedChoice::Discard:
        return true;
    case UnsavedChoice::Save:
        return !closing;
    case UnsavedChoice::Cancel:
        return false;
    }
    return false;
}

}
