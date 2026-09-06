#include "ChoiceDialogLayout.h"

#include <algorithm>

namespace GameEngine::Platform
{
namespace
{
    constexpr short Margin = 7;
    constexpr short MessageLineHeight = 9;
    constexpr short MinimumMessageHeight = 26;
    constexpr short ButtonHeight = 14;
    constexpr short ButtonGap = 6;
    constexpr short MinimumButtonWidth = 72;
    constexpr short MinimumContentWidth = 240;

    // 대화상자 단위의 정의가 곧 이 값이다: 가로 4단위가 대화상자 글꼴의 평균 글자 폭 하나다.
    // 그래서 글자 수에 4를 곱하면 그 글이 차지할 폭이 나온다.
    constexpr short WidthPerCharacter = 4;
    // 글 양옆의 여백이다. 이것이 없으면 글자가 버튼 테두리에 닿는다.
    constexpr short ButtonTextPadding = 12;

    [[nodiscard]] short MeasureButtonWidth(const std::string& label)
    {
        const short text = static_cast<short>(
            (std::min)(static_cast<std::size_t>(200), label.size()) * WidthPerCharacter);
        return (std::max)(MinimumButtonWidth, static_cast<short>(text + ButtonTextPadding));
    }
}

ChoiceDialogLayout ComputeChoiceDialogLayout(
    const std::vector<std::string>& buttonLabels, const std::size_t messageLineCount)
{
    ChoiceDialogLayout layout;

    short buttonRowWidth = 0;
    std::vector<short> widths;
    widths.reserve(buttonLabels.size());
    for (const std::string& label : buttonLabels)
    {
        const short width = MeasureButtonWidth(label);
        widths.push_back(width);
        buttonRowWidth = static_cast<short>(buttonRowWidth + width);
    }
    if (!widths.empty())
    {
        buttonRowWidth = static_cast<short>(
            buttonRowWidth + static_cast<short>(widths.size() - 1) * ButtonGap);
    }

    const short contentWidth = (std::max)(buttonRowWidth, MinimumContentWidth);
    const short messageHeight = (std::max)(
        MinimumMessageHeight,
        static_cast<short>((std::max)(std::size_t{ 1 }, messageLineCount) * MessageLineHeight));

    layout.width = static_cast<short>(contentWidth + Margin * 2);
    layout.message = { Margin, Margin, contentWidth, messageHeight };

    const short buttonTop = static_cast<short>(Margin + messageHeight + Margin);
    layout.height = static_cast<short>(buttonTop + ButtonHeight + Margin);

    // 버튼 줄은 내용 폭 안에서 가운데에 놓는다.
    short buttonLeft = static_cast<short>(Margin + (contentWidth - buttonRowWidth) / 2);
    layout.buttons.reserve(widths.size());
    for (const short width : widths)
    {
        layout.buttons.push_back({ buttonLeft, buttonTop, width, ButtonHeight });
        buttonLeft = static_cast<short>(buttonLeft + width + ButtonGap);
    }
    return layout;
}

}
