#pragma once

// editor-layer: 0 (Rules)

#include <cstddef>
#include <optional>

namespace GameEditor
{

/// <summary>
/// 어느 메뉴가 펼쳐져 있는지와, 그것이 열리고 닫히는 규칙이다.
///
/// 자리도 폭도 모른다 — 무엇이 열려 있는지만 안다. 그래서 창 없이 시험되고, 배치가 바뀌어도
/// 이 규칙은 그대로다.
///
/// 메뉴는 <b>모달이 아니다.</b> 모달은 밖을 눌러도 아무 일이 없는 것이고, 메뉴는 밖을 누르면
/// 닫히는 것이라 다른 몸짓이다. 그 차이가 이 타입이 존재하는 이유의 절반이며, 나머지 절반은
/// 「같은 머리를 다시 누르면 닫힌다」처럼 눈으로만 확인하기 쉬운 규칙을 수로 못박는 것이다.
/// </summary>
class EditorMenuBarState final
{
public:
    /// <summary>펼쳐진 메뉴의 번호다. 아무것도 열려 있지 않으면 값이 없다.</summary>
    [[nodiscard]] std::optional<std::size_t> GetOpenMenu() const { return mOpenMenu; }
    [[nodiscard]] bool IsOpen() const { return mOpenMenu.has_value(); }

    /// <summary>
    /// 머리줄을 눌렀다. 닫혀 있으면 열고, 같은 것이 열려 있으면 닫고, 다른 것이 열려 있으면
    /// 그쪽으로 옮긴다 — 메뉴 막대에서 사람이 기대하는 그대로다.
    /// </summary>
    /// <param name="menuIndex">눌린 머리줄의 번호다.</param>
    void PressHeading(std::size_t menuIndex);

    /// <summary>
    /// 이번 프레임의 누름이 메뉴 밖에서 일어났음을 알린다. 열려 있던 메뉴는 닫힌다.
    ///
    /// 머리줄과 펼친 목록은 「밖」이 아니다: 머리줄의 누름은 <see cref="PressHeading"/>이
    /// 다루고, 목록 안의 누름은 항목을 고르는 것이다. 그 둘을 여기서 다시 판정하지 않는 이유는,
    /// 무엇이 어디에 있는지가 이 타입이 모르는 것이기 때문이다 — 부르는 쪽이 안다.
    /// </summary>
    void PressOutside();

    /// <summary>
    /// 항목을 골랐다. 고른 뒤 메뉴는 닫힌다 — 명령이 실행되는데 목록이 남아 있으면 사람은
    /// 그 명령이 아직 실행되지 않았다고 읽는다.
    /// </summary>
    void ChooseItem();

    /// <summary>Esc다. 아무것도 고르지 않고 닫는다.</summary>
    void Cancel();

private:
    std::optional<std::size_t> mOpenMenu;
};

}
