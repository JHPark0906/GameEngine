#pragma once

#include <cstddef>
#include <vector>

namespace GameEngine::UI
{

/// <summary>
/// 슬롯 N개에 패널 N개가 정확히 하나씩 놓이는 배정이다 — 패널 번호들의 순열.
///
/// 도킹 UI의 데이터 부분만 담는다: 어느 패널이 어느 슬롯에 있는지, 그리고 두 패널의 자리를
/// 맞바꾸는 연산. 슬롯의 사각형이 어디이고 드래그가 어떻게 읽히는지는 이 타입의 일이 아니라서,
/// 배정과 스왑은 화면 없이 테스트된다. 패널과 슬롯은 둘 다 0부터 세는 번호다 — 이름은 이 값을
/// 쓰는 쪽의 enum이 붙인다.
/// </summary>
class PanelSlotLayout final
{
public:
    PanelSlotLayout() = default;

    /// <summary>패널 i가 슬롯 i에 놓인 기본 배정으로 시작한다.</summary>
    explicit PanelSlotLayout(std::size_t slotCount);

    [[nodiscard]] std::size_t GetSlotCount() const { return mSlotOfPanel.size(); }

    /// <summary>패널이 놓인 슬롯이다. 범위 밖 패널이면 슬롯 수를 반환한다.</summary>
    [[nodiscard]] std::size_t GetSlotOfPanel(std::size_t panelIndex) const;

    /// <summary>슬롯에 놓인 패널이다. 범위 밖 슬롯이면 슬롯 수를 반환한다.</summary>
    [[nodiscard]] std::size_t GetPanelInSlot(std::size_t slotIndex) const;

    /// <summary>
    /// 두 패널의 슬롯을 맞바꾼다. 어느 쪽이든 범위 밖이면 아무것도 바꾸지 않고 false다.
    /// 같은 패널 둘은 유효한 무변화 스왑이다 — 드롭이 제자리에 떨어진 경우가 이것이다.
    /// </summary>
    bool SwapPanels(std::size_t firstPanelIndex, std::size_t secondPanelIndex);

    /// <summary>
    /// 슬롯별 패널 배정을 통째로 바꾼다. 크기가 다르거나 순열이 아니면 아무것도 바꾸지 않는다
    /// — 저장돼 있던 레이아웃을 되살리는 쪽이 손상된 파일을 그대로 믿지 않게 하는 검증이
    /// 이것이다.
    /// </summary>
    /// <param name="panelInSlot">슬롯 i에 놓일 패널 번호들이다. 슬롯 수와 같은 길이의 순열이어야 한다.</param>
    /// <returns>배정을 받아들였으면 true이고, 거절이면 기존 배정이 그대로다.</returns>
    bool TrySetAssignment(const std::vector<std::size_t>& panelInSlot);

private:
    /// <summary>패널 번호로 찾는 슬롯 번호다. 순열이므로 역방향 조회는 값 탐색으로 충분하다.</summary>
    std::vector<std::size_t> mSlotOfPanel;
};

}
