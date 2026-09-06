#include "pch.h"
#include "TextFit.h"

#include <string>
#include <vector>

namespace GameEngine::Core
{

namespace
{
    /// <summary>이 바이트가 UTF-8 문자의 이어지는 조각인지다. 문자 경계는 그렇지 않은 자리다.</summary>
    [[nodiscard]] bool IsContinuationByte(const char byte)
    {
        return (static_cast<unsigned char>(byte) & 0xC0u) == 0x80u;
    }
}

TextFitResult FitTextToWidth(
    const std::string_view text,
    const float maxWidth,
    const std::string_view ellipsis,
    const std::function<float(std::string_view)>& measure)
{
    TextFitResult result;
    if (text.empty() || !measure)
    {
        result.length = text.size();
        return result;
    }
    if (maxWidth <= 0.0f)
    {
        result.truncated = true;
        return result;
    }
    if (measure(text) <= maxWidth)
    {
        result.length = text.size();
        return result;
    }

    // 자를 수 있는 자리들이다. 0은 언제나 후보이며 — 말줄임표만 남는 경우다 — 원문 전체는
    // 이미 들어가지 않는 것으로 판명됐으므로 후보가 아니다.
    std::vector<std::size_t> boundaries;
    boundaries.push_back(0);
    for (std::size_t index = 1; index < text.size(); ++index)
    {
        if (!IsContinuationByte(text[index]))
        {
            boundaries.push_back(index);
        }
    }

    // 들어가는 경계 중 가장 뒤를 찾는다. 폭은 글자가 늘수록 늘어나므로 이분 탐색이 성립하고,
    // 그래서 긴 문구도 잣대를 몇 번만 부른다 — 잣대 한 번이 글자 배치 한 번이라 값이 싸지 않다.
    std::string candidate;
    const auto fits = [&](const std::size_t length)
    {
        candidate.assign(text.substr(0, length));
        candidate.append(ellipsis);
        return measure(candidate) <= maxWidth;
    };

    std::size_t low = 0;
    std::size_t high = boundaries.size();
    while (low < high)
    {
        const std::size_t middle = low + (high - low) / 2;
        if (fits(boundaries[middle]))
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }

    result.truncated = true;
    // low가 0이면 말줄임표조차 들어가지 않은 것이다. 그때도 잘렸다고 답하는 편이 옳다:
    // 호출자가 보여 줄 것이 없다는 사실을 알아야 자리를 다르게 쓸 수 있다.
    result.length = low > 0 ? boundaries[low - 1] : 0;
    return result;
}

}
