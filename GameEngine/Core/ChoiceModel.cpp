#include "pch.h"
#include "ChoiceModel.h"

namespace GameEngine::Core
{

void ChoiceModel::SetSelected(const std::size_t index, const std::size_t optionCount)
{
    mSelected = index < optionCount ? index : NoSelection;
}

void ChoiceModel::ClampTo(const std::size_t optionCount)
{
    if (mSelected != NoSelection && mSelected >= optionCount)
    {
        mSelected = NoSelection;
    }
    if (optionCount == 0)
    {
        mHighlighted = 0;
    }
    else if (mHighlighted >= optionCount)
    {
        mHighlighted = optionCount - 1;
    }
}

void ChoiceModel::Open(const std::size_t optionCount)
{
    mOpen = true;
    mHighlighted = mSelected != NoSelection && mSelected < optionCount ? mSelected : 0;
}

void ChoiceModel::Close()
{
    mOpen = false;
}

bool ChoiceModel::Choose(const std::size_t index, const std::size_t optionCount)
{
    mOpen = false;
    if (index >= optionCount)
    {
        return false;
    }
    const bool changed = mSelected != index;
    mSelected = index;
    return changed;
}

ChoiceModel::Result ChoiceModel::Apply(const std::size_t optionCount, const Input& input)
{
    ClampTo(optionCount);
    const bool wasOpen = mOpen;
    Result result;

    if (input.toggle)
    {
        if (mOpen)
        {
            Close();
        }
        else
        {
            Open(optionCount);
        }
    }

    if (mOpen)
    {
        if (input.hovered && *input.hovered < optionCount)
        {
            mHighlighted = *input.hovered;
        }
        if (input.moveUp && mHighlighted > 0)
        {
            --mHighlighted;
        }
        if (input.moveDown && optionCount > 0 && mHighlighted + 1 < optionCount)
        {
            ++mHighlighted;
        }

        if (input.clicked)
        {
            result.selectionChanged = Choose(*input.clicked, optionCount);
        }
        else if (input.confirm)
        {
            result.selectionChanged = Choose(mHighlighted, optionCount);
        }
        else if (input.cancel)
        {
            Close();
        }
    }

    result.openChanged = wasOpen != mOpen;
    return result;
}

}
