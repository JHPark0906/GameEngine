#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

namespace GameEngine::Core
{

/// <summary>글자를 주어진 폭에 맞춘 결과다.</summary>
struct TextFitResult
{
    /// <summary>
    /// 그려야 할 접두사의 바이트 길이다. 잘리지 않았으면 원문 전체 길이이고, 잘렸으면 말줄임표
    /// 앞까지의 길이다. 폭이 말줄임표 하나도 못 담을 만큼 좁으면 0이며, 그때 남는 것은
    /// 말줄임표뿐이다.
    /// </summary>
    std::size_t length = 0;

    /// <summary>잘렸는지다. 참이면 접두사 뒤에 말줄임표를 붙여 그린다.</summary>
    bool truncated = false;
};

/// <summary>
/// 글자를 주어진 폭 안에 들어가도록 줄인다.
///
/// 재는 일과 줄이는 일을 나누는 자리다. 폭을 재려면 폰트를 알아야 하지만 — 그래서 재는 쪽은
/// 글자를 배치하는 계층에 있다 — 어디서 자를지는 잰 값들 사이의 순수한 판단이라 폰트를 몰라도
/// 된다. 그래서 이 규칙은 즉시 모드 위젯과 유지 모드 레이아웃이 각자의 잣대를 들고 와서 함께
/// 쓸 수 있다.
///
/// 자르는 자리는 UTF-8 문자 경계다. 바이트 한가운데를 자르면 한글 한 글자가 깨진 바이트로
/// 남는다.
/// </summary>
/// <param name="text">맞출 원문이다.</param>
/// <param name="maxWidth">쓸 수 있는 폭이다. 0 이하면 아무것도 들어가지 않는다.</param>
/// <param name="ellipsis">잘렸을 때 뒤에 붙일 글자다. 이것의 폭도 예산에 든다.</param>
/// <param name="measure">문자열 하나의 폭을 재는 잣대다.</param>
/// <returns>그릴 접두사의 길이와 잘렸는지다.</returns>
[[nodiscard]] TextFitResult FitTextToWidth(
    std::string_view text,
    float maxWidth,
    std::string_view ellipsis,
    const std::function<float(std::string_view)>& measure);

}
