#include "Rules/EditorMenuBarState.h"

namespace GameEditor
{

void EditorMenuBarState::PressHeading(const std::size_t menuIndex)
{
    if (mOpenMenu == menuIndex)
    {
        mOpenMenu.reset();
        return;
    }
    mOpenMenu = menuIndex;
}

void EditorMenuBarState::PressOutside()
{
    mOpenMenu.reset();
}

void EditorMenuBarState::ChooseItem()
{
    mOpenMenu.reset();
}

void EditorMenuBarState::Cancel()
{
    mOpenMenu.reset();
}

}
