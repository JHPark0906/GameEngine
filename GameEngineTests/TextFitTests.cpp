#include <iostream>
#include <string>
#include <string_view>

#include "../GameEngine/Core/TextFit.h"

#include "TextFitTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>
    /// 바이트 하나를 10픽셀로 세는 가짜 잣대다. 폰트 없이도 자를 자리의 규칙을 볼 수 있고,
    /// 기대값이 손으로 셀 수 있는 수가 된다.
    /// </summary>
    [[nodiscard]] float MeasureByBytes(const std::string_view text)
    {
        return static_cast<float>(text.size()) * 10.0f;
    }

    constexpr std::string_view Ellipsis = "\xE2\x80\xA6";
}

bool RunTextFitTests()
{
    std::cout << "running text fit tests\n";
    bool passed = true;

    {
        // 들어가는 글자는 손대지 않는다.
        const GameEngine::Core::TextFitResult fit =
            GameEngine::Core::FitTextToWidth("abc", 100.0f, Ellipsis, MeasureByBytes);
        passed = Expect(
            fit.length == 3 && !fit.truncated,
            "text that already fits should be left alone") && passed;
    }

    {
        // 말줄임표가 3바이트=30픽셀이므로, 80픽셀에는 접두사 5바이트까지 들어간다.
        const GameEngine::Core::TextFitResult fit =
            GameEngine::Core::FitTextToWidth("abcdefghij", 80.0f, Ellipsis, MeasureByBytes);
        passed = Expect(
            fit.truncated && fit.length == 5,
            "the ellipsis should be paid for out of the same budget") && passed;
    }

    {
        // 자르는 자리는 UTF-8 문자 경계다. 한글 한 글자가 3바이트이므로 길이는 3의 배수다.
        const GameEngine::Core::TextFitResult fit = GameEngine::Core::FitTextToWidth(
            "\xED\x95\x9C\xEA\xB8\x80\xEC\x9E\x85\xEB\xA0\xA5", 90.0f, Ellipsis, MeasureByBytes);
        passed = Expect(
            fit.truncated && fit.length == 6,
            "a cut should land on a character boundary, never inside one") && passed;
    }

    {
        // 말줄임표조차 들어가지 않는 폭이다. 남는 것이 없다는 사실이 호출자에게 전해져야 한다.
        const GameEngine::Core::TextFitResult fit =
            GameEngine::Core::FitTextToWidth("abcdef", 10.0f, Ellipsis, MeasureByBytes);
        passed = Expect(
            fit.truncated && fit.length == 0,
            "a width too narrow for even the ellipsis should leave nothing") && passed;
    }

    {
        const GameEngine::Core::TextFitResult fit =
            GameEngine::Core::FitTextToWidth("abc", 0.0f, Ellipsis, MeasureByBytes);
        passed = Expect(
            fit.truncated && fit.length == 0,
            "a rect with no width should hold no text") && passed;
    }

    {
        const GameEngine::Core::TextFitResult fit =
            GameEngine::Core::FitTextToWidth("", 100.0f, Ellipsis, MeasureByBytes);
        passed = Expect(
            fit.length == 0 && !fit.truncated,
            "empty text is not truncated text") && passed;
    }

    return passed;
}

static const TestSupport::Registration gTextFitTests{
    "Core", "text fit tests should pass", RunTextFitTests };
