#include "pch.h"
#include "PanelSlotLayout.h"

#include <cstddef>
#include <numeric>
#include <utility>

namespace GameEngine::UI
{

PanelSlotLayout::PanelSlotLayout(const std::size_t slotCount)
    : mSlotOfPanel(slotCount)
{
    std::iota(mSlotOfPanel.begin(), mSlotOfPanel.end(), std::size_t{ 0 });
}

std::size_t PanelSlotLayout::GetSlotOfPanel(const std::size_t panelIndex) const
{
    if (panelIndex >= mSlotOfPanel.size())
    {
        return mSlotOfPanel.size();
    }
    return mSlotOfPanel[panelIndex];
}

std::size_t PanelSlotLayout::GetPanelInSlot(const std::size_t slotIndex) const
{
    for (std::size_t panelIndex = 0; panelIndex < mSlotOfPanel.size(); ++panelIndex)
    {
        if (mSlotOfPanel[panelIndex] == slotIndex)
        {
            return panelIndex;
        }
    }
    return mSlotOfPanel.size();
}

bool PanelSlotLayout::SwapPanels(
    const std::size_t firstPanelIndex, const std::size_t secondPanelIndex)
{
    if (firstPanelIndex >= mSlotOfPanel.size() || secondPanelIndex >= mSlotOfPanel.size())
    {
        return false;
    }
    std::swap(mSlotOfPanel[firstPanelIndex], mSlotOfPanel[secondPanelIndex]);
    return true;
}

bool PanelSlotLayout::TrySetAssignment(const std::vector<std::size_t>& panelInSlot)
{
    if (panelInSlot.size() != mSlotOfPanel.size())
    {
        return false;
    }
    // 순열 검증: 각 패널이 정확히 한 번씩 나와야 한다. 아니면 어떤 패널은 두 번 그려지고 어떤
    // 패널은 사라진다.
    std::vector<bool> seen(panelInSlot.size(), false);
    for (const std::size_t panelIndex : panelInSlot)
    {
        if (panelIndex >= panelInSlot.size() || seen[panelIndex])
        {
            return false;
        }
        seen[panelIndex] = true;
    }
    for (std::size_t slotIndex = 0; slotIndex < panelInSlot.size(); ++slotIndex)
    {
        mSlotOfPanel[panelInSlot[slotIndex]] = slotIndex;
    }
    return true;
}

}
