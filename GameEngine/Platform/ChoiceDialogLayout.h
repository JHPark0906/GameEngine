#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace GameEngine::Platform
{

/// <summary>선택 대화상자 안에서 조각 하나가 차지하는 자리다. 단위는 대화상자 단위(du)다.</summary>
struct ChoiceDialogRectangle
{
    short left = 0;
    short top = 0;
    short width = 0;
    short height = 0;

    /// <summary>오른쪽 변이다.</summary>
    [[nodiscard]] short GetRight() const { return static_cast<short>(left + width); }
};

/// <summary>
/// 선택 대화상자의 조각들이 놓일 자리다. 창을 만들기 전에 정해지는 순수한 산술이라 창 없이
/// 잴 수 있고, 버튼이 잘리거나 겹치는지를 눈이 아니라 사각형으로 답한다.
/// </summary>
struct ChoiceDialogLayout
{
    /// <summary>대화상자 전체 크기다.</summary>
    short width = 0;
    short height = 0;
    /// <summary>설명 글이 놓일 자리다.</summary>
    ChoiceDialogRectangle message;
    /// <summary>버튼들이 놓일 자리다. 요청한 순서대로이며, 취소 버튼이 있으면 맨 뒤다.</summary>
    std::vector<ChoiceDialogRectangle> buttons;
};

/// <summary>
/// 버튼 라벨들로 대화상자의 자리를 계산한다. 버튼 폭이 라벨을 따라 늘어나는 이유는, 고정
/// 폭이면 긴 라벨이 소리 없이 잘려 사람이 무엇을 누르는지 모르게 되기 때문이다. 대화상자의
/// 폭은 버튼 줄을 담을 만큼 함께 늘어나므로 버튼끼리 겹치지 않는다.
/// </summary>
/// <param name="buttonLabels">버튼에 적힐 글들이다. 순서가 놓이는 순서다.</param>
/// <param name="messageLineCount">설명 글이 차지할 줄 수다. 최소 한 줄이다.</param>
/// <returns>계산된 자리다. 버튼이 없으면 버튼 목록이 비어 있다.</returns>
[[nodiscard]] ChoiceDialogLayout ComputeChoiceDialogLayout(
    const std::vector<std::string>& buttonLabels, std::size_t messageLineCount);

}
