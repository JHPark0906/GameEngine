#pragma once

#include <cstddef>
#include <limits>
#include <optional>

namespace GameEngine::Core
{

/// <summary>
/// 목록에서 하나를 고르는 위젯의 상태 기계다: 펼침 여부, 강조된 항목, 선택된 항목, 그리고 그
/// 셋을 움직이는 입력.
///
/// 위젯도 렌더링도 모른다. 아는 것은 항목 수와 인덱스뿐이라 화면 없이 시험할 수 있다. 항목의
/// 내용을 소유하지 않고 연산마다 개수를 받는 이유는 <see cref="TextEditModel"/>이 문자열을
/// 받는 이유와 같다 — 즉시 모드 위젯은 매 프레임 자기 목록을 넘기고, 유지 모드 컴포넌트는 자기
/// 멤버 목록을 넘기되, 두 UI가 같은 규칙을 쓴다.
/// </summary>
class ChoiceModel final
{
public:
    /// <summary>아무것도 선택되지 않았음을 뜻하는 인덱스다.</summary>
    static constexpr std::size_t NoSelection = (std::numeric_limits<std::size_t>::max)();

    /// <summary>
    /// 이번 프레임에 도착한 입력이다. 키 하나하나가 아니라 "무엇이 요청됐는가"이므로, 어느 키나
    /// 클릭이 그 요청을 만드는지는 위젯의 몫이다.
    /// </summary>
    struct Input
    {
        /// <summary>닫혀 있으면 펼치고 펼쳐 있으면 접는다. 머리 칸을 누른 것이 이것이다.</summary>
        bool toggle = false;
        /// <summary>펼쳐 있을 때 강조를 한 칸 위로 옮긴다.</summary>
        bool moveUp = false;
        /// <summary>펼쳐 있을 때 강조를 한 칸 아래로 옮긴다.</summary>
        bool moveDown = false;
        /// <summary>펼쳐 있을 때 강조된 항목을 고르고 접는다.</summary>
        bool confirm = false;
        /// <summary>펼쳐 있을 때 고르지 않고 접는다.</summary>
        bool cancel = false;
        /// <summary>커서가 놓인 항목이다. 펼쳐 있을 때 강조가 그 항목으로 간다.</summary>
        std::optional<std::size_t> hovered;
        /// <summary>눌린 항목이다. 펼쳐 있을 때 그 항목을 고르고 접는다.</summary>
        std::optional<std::size_t> clicked;
    };

    /// <summary>입력 한 번이 남긴 것이다. 호출자가 화면과 값을 여기에 맞춘다.</summary>
    struct Result
    {
        /// <summary>선택된 항목이 바뀌었는지다. 위젯의 "값이 바뀌었다"가 이것이다.</summary>
        bool selectionChanged = false;
        /// <summary>펼침 여부가 바뀌었는지다.</summary>
        bool openChanged = false;
    };

    [[nodiscard]] bool IsOpen() const { return mOpen; }

    /// <summary>선택된 항목의 인덱스이며, 없으면 <see cref="NoSelection"/>이다.</summary>
    [[nodiscard]] std::size_t GetSelected() const { return mSelected; }

    /// <summary>펼쳐 있을 때 강조된 항목이다. 접혀 있으면 뜻이 없다.</summary>
    [[nodiscard]] std::size_t GetHighlighted() const { return mHighlighted; }

    /// <summary>
    /// 선택을 밖에서 정한다. 값이 바뀌어 위젯이 따라가야 할 때 쓰며, 범위 밖이면 선택이 없어진다.
    /// </summary>
    void SetSelected(std::size_t index, std::size_t optionCount);

    /// <summary>
    /// 항목 수가 바뀐 뒤 상태를 그 범위 안으로 되돌린다. 선택이 범위를 벗어났으면 선택이 없어지고,
    /// 강조는 마지막 항목으로 물러난다.
    /// </summary>
    void ClampTo(std::size_t optionCount);

    /// <summary>펼친다. 강조는 선택된 항목에서, 선택이 없으면 첫 항목에서 시작한다.</summary>
    void Open(std::size_t optionCount);

    /// <summary>고르지 않고 접는다.</summary>
    void Close();

    /// <summary>이번 프레임의 입력을 적용한다.</summary>
    /// <param name="optionCount">지금 목록에 있는 항목 수다.</param>
    /// <param name="input">이번 프레임의 요청들이다.</param>
    /// <returns>무엇이 바뀌었는지다.</returns>
    [[nodiscard]] Result Apply(std::size_t optionCount, const Input& input);

private:
    /// <summary>항목을 고르고 접는다. 바뀌었으면 true다.</summary>
    bool Choose(std::size_t index, std::size_t optionCount);

    bool mOpen = false;
    std::size_t mSelected = NoSelection;
    std::size_t mHighlighted = 0;
};

}
