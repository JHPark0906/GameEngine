#include "Type2CharstringTests.h"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <span>
#include <vector>

#include "Text/Type2Charstring.h"
#include "TestSupport.h"

using TestSupport::Expect;

/// <summary>
/// 해석기의 두 함정을 <b>손으로 지은 charstring</b>으로 묻는다.
///
/// 실제 폰트로는 이것을 물을 수 없다. 폰트가 그 규칙을 지켜 만들어졌으므로, 규칙을 어긴 해석기도
/// 「대체로 맞는」 그림을 내고 그림이 맞는지 아닌지는 눈으로만 갈린다. 바이트를 우리가 지으면
/// 규칙 하나만 정확히 겨눌 수 있다 — 이 두 함정은 <b>틀려도 죽지 않고 글자가 미묘하게 이상해지는</b>
/// 종류라, 나중에 발견하면 원인을 찾을 수 없다.
/// </summary>
namespace
{
    using namespace GameEngine::Text;

    /// <summary>Type 2의 정수 인코딩이다. -107..107은 한 바이트다.</summary>
    void PushSmall(std::vector<std::byte>& bytes, const int value)
    {
        bytes.push_back(static_cast<std::byte>(value + 139));
    }

    /// <summary>16비트 정수는 28 뒤에 큰 끝으로 붙는다.</summary>
    void PushInt16(std::vector<std::byte>& bytes, const int value)
    {
        bytes.push_back(std::byte{ 28 });
        bytes.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
        bytes.push_back(static_cast<std::byte>(value & 0xFF));
    }

    void PushOperator(std::vector<std::byte>& bytes, const unsigned char op)
    {
        bytes.push_back(static_cast<std::byte>(op));
    }

    /// <summary>12로 시작하는 두 바이트 연산자다. flex 사인방이 여기 있다.</summary>
    void PushEscape(std::vector<std::byte>& bytes, const unsigned char op)
    {
        bytes.push_back(std::byte{ 12 });
        bytes.push_back(static_cast<std::byte>(op));
    }

    constexpr unsigned char OpHStem = 1;
    constexpr unsigned char OpVStem = 3;
    constexpr unsigned char OpRMoveTo = 21;
    constexpr unsigned char OpRLineTo = 5;
    constexpr unsigned char OpEndChar = 14;
    constexpr unsigned char OpHintMask = 19;
    constexpr unsigned char OpRRCurveTo = 8;
    constexpr unsigned char EscapeFlex = 35;
    constexpr unsigned char EscapeHFlex = 34;
    constexpr unsigned char EscapeHFlex1 = 36;
    constexpr unsigned char EscapeFlex1 = 37;

    [[nodiscard]] CharstringContext EmptyContext()
    {
        CharstringContext context;
        context.defaultWidthX = 0.0f;
        context.nominalWidthX = 0.0f;
        return context;
    }

    /// <summary>윤곽선이 실제로 지나간 점들을 모은다. 끝점만 보면 곡선의 형태를 알 수 있다.</summary>
    [[nodiscard]] std::vector<OutlinePoint> EndPoints(const GlyphOutline& outline)
    {
        std::vector<OutlinePoint> points;
        for (const OutlineContour& contour : outline.contours)
        {
            points.push_back(contour.start);
            for (const OutlineSegment& segment : contour.segments)
            {
                points.push_back(segment.end);
            }
        }
        return points;
    }

    [[nodiscard]] bool Near(const float value, const float expected, const float slack = 0.01f)
    {
        return std::abs(value - expected) < slack;
    }

    /// <summary>
    /// 🪤 함정 하나: <c>hintmask</c> 앞의 스택은 적히지 않은 <c>vstem</c>이다.
    ///
    /// 여기서는 <c>hstem</c>을 둘 세우고, <c>vstem</c>을 <b>적지 않은 채</b> 값 넷만 스택에
    /// 남기고 <c>hintmask</c>를 부른다. 규칙대로면 스템은 2+2=4개라 마스크는 <b>한 바이트</b>다.
    /// 암묵 vstem을 세지 않는 해석기도 4개를 8로 나누면 한 바이트라 같은 값이 나와 버리므로,
    /// 스템을 <b>아홉 개</b>로 만들어 <b>두 바이트</b>가 되게 한다 — 그래야 한 바이트만 건너뛴
    /// 해석기가 마스크의 둘째 바이트를 명령으로 읽고 어긋난다.
    /// </summary>
    [[nodiscard]] bool CheckHintMaskCountsImplicitVerticalStems()
    {
        std::vector<std::byte> charstring;
        // hstem 다섯 개: 값 열 개.
        for (int stem = 0; stem < 5; ++stem)
        {
            PushSmall(charstring, 10 * stem);
            PushSmall(charstring, 5);
        }
        PushOperator(charstring, OpHStem);
        // vstem 네 개를 값만 남기고 연산자 없이 hintmask로 넘긴다. 이것이 암묵 vstem이다.
        for (int stem = 0; stem < 4; ++stem)
        {
            PushSmall(charstring, 10 * stem);
            PushSmall(charstring, 5);
        }
        PushOperator(charstring, OpHintMask);
        // 스템 아홉 개 -> 마스크 두 바이트.
        charstring.push_back(std::byte{ 0xFF });
        charstring.push_back(std::byte{ 0x80 });
        // 그 다음이 진짜 그림이다. 삼각형 하나.
        PushSmall(charstring, 100);
        PushSmall(charstring, 0);
        PushOperator(charstring, OpRMoveTo);
        PushSmall(charstring, 50);
        PushSmall(charstring, 0);
        PushOperator(charstring, OpRLineTo);
        PushSmall(charstring, 0);
        PushSmall(charstring, 50);
        PushOperator(charstring, OpRLineTo);
        PushOperator(charstring, OpEndChar);

        CharstringResult result;
        const CharstringContext context = EmptyContext();
        if (!Expect(
                RunType2Charstring(charstring, context, result),
                "a charstring with an implicit vstem before hintmask runs"))
        {
            return false;
        }

        std::cout << "  hintmask trap: " << result.stemCount << " stems counted (9 expected), "
                  << result.outline.contours.size() << " contour(s)\n";

        bool passed = Expect(
            result.stemCount == 9,
            "the values left on the stack before hintmask count as vertical stems");
        // 마스크를 한 바이트만 건너뛰면 0x80이 연산자로 읽혀 그림이 달라진다. 시작점이 그 증거다.
        const std::vector<OutlinePoint> points = EndPoints(result.outline);
        if (!Expect(!points.empty(), "and the drawing after the mask survives"))
        {
            return false;
        }
        if (!Near(points.front().x, 100.0f) || !Near(points.front().y, 0.0f))
        {
            std::cerr << "  the outline starts at (" << points.front().x << ","
                      << points.front().y << ") instead of (100,0), so the mask was not skipped "
                      << "by the right number of bytes\n";
        }
        passed &= Expect(
            Near(points.front().x, 100.0f) && Near(points.front().y, 0.0f),
            "and the operators after the mask are read from the right place");
        return passed;
    }

    /// <summary>
    /// 🪤 함정 둘: flex 사인방은 생략된 좌표를 저마다 다르게 채운다.
    ///
    /// 넷 모두 「거의 평평한 곡선 두 개」를 적는 축약이고, 어느 좌표를 생략하고 무엇으로 채우는지가
    /// 다르다. 같은 모양을 <c>rrcurveto</c> 두 번으로도 적을 수 있으므로, 둘이 같은 점들을
    /// 지나는지 견주면 규칙을 지켰는지 알 수 있다 — 우리가 기대값을 지어내지 않는 방법이다.
    /// </summary>
    [[nodiscard]] bool CheckFlexMatchesTwoCurves()
    {
        // hflex: dx1 dx2 dy2 dx3 dx4 dx5 dx6. y는 시작 높이로 돌아온다.
        std::vector<std::byte> flexed;
        PushSmall(flexed, 0);
        PushSmall(flexed, 0);
        PushOperator(flexed, OpRMoveTo);
        PushSmall(flexed, 10);   // dx1
        PushSmall(flexed, 10);   // dx2
        PushSmall(flexed, 20);   // dy2
        PushSmall(flexed, 10);   // dx3
        PushSmall(flexed, 10);   // dx4
        PushSmall(flexed, 10);   // dx5
        PushSmall(flexed, 10);   // dx6
        PushEscape(flexed, EscapeHFlex);
        PushOperator(flexed, OpEndChar);

        // 같은 것을 rrcurveto 둘로 적는다. hflex의 규칙대로 채우면 이렇게 된다:
        // 첫 곡선 (10,0) (10,20) (10,0), 둘째 (10,0) (10,-20) (10,0).
        std::vector<std::byte> explicitCurves;
        PushSmall(explicitCurves, 0);
        PushSmall(explicitCurves, 0);
        PushOperator(explicitCurves, OpRMoveTo);
        PushSmall(explicitCurves, 10);
        PushSmall(explicitCurves, 0);
        PushSmall(explicitCurves, 10);
        PushSmall(explicitCurves, 20);
        PushSmall(explicitCurves, 10);
        PushSmall(explicitCurves, 0);
        PushOperator(explicitCurves, OpRRCurveTo);
        PushSmall(explicitCurves, 10);
        PushSmall(explicitCurves, 0);
        PushSmall(explicitCurves, 10);
        PushSmall(explicitCurves, -20);
        PushSmall(explicitCurves, 10);
        PushSmall(explicitCurves, 0);
        PushOperator(explicitCurves, OpRRCurveTo);
        PushOperator(explicitCurves, OpEndChar);

        const CharstringContext context = EmptyContext();
        CharstringResult viaFlex;
        CharstringResult viaCurves;
        if (!Expect(
                RunType2Charstring(flexed, context, viaFlex) &&
                    RunType2Charstring(explicitCurves, context, viaCurves),
                "both spellings of the same shape run"))
        {
            return false;
        }

        const std::vector<OutlinePoint> flexPoints = EndPoints(viaFlex.outline);
        const std::vector<OutlinePoint> curvePoints = EndPoints(viaCurves.outline);
        std::cout << "  hflex trap: " << flexPoints.size() << " points via hflex, "
                  << curvePoints.size() << " via two rrcurveto\n";
        if (!Expect(
                flexPoints.size() == curvePoints.size(),
                "hflex draws as many points as the two curves it stands for"))
        {
            return false;
        }

        bool same = true;
        for (std::size_t index = 0; index < flexPoints.size(); ++index)
        {
            if (!Near(flexPoints[index].x, curvePoints[index].x) ||
                !Near(flexPoints[index].y, curvePoints[index].y))
            {
                std::cerr << "  point " << index << " is (" << flexPoints[index].x << ","
                          << flexPoints[index].y << ") via hflex but (" << curvePoints[index].x
                          << "," << curvePoints[index].y << ") via two curves\n";
                same = false;
            }
        }
        return Expect(same, "and puts them in the same places");
    }

    /// <summary>
    /// 같은 모양을 rrcurveto 둘로 표현해 나머지 세 명령의 결과와 비교한다.
    /// 네 명령이 각각 다른 좌표를 생략하므로 모든 명령을 독립적으로 검사해야 한다.
    /// </summary>
    [[nodiscard]] bool CheckFlexFormAgainstTwoCurves(
        const unsigned char escape,
        const std::vector<int>& flexArguments,
        const std::vector<int>& firstCurve,
        const std::vector<int>& secondCurve,
        const char* const what)
    {
        std::vector<std::byte> flexed;
        PushSmall(flexed, 0);
        PushSmall(flexed, 0);
        PushOperator(flexed, OpRMoveTo);
        for (const int value : flexArguments)
        {
            PushInt16(flexed, value);
        }
        PushEscape(flexed, escape);
        PushOperator(flexed, OpEndChar);

        std::vector<std::byte> explicitCurves;
        PushSmall(explicitCurves, 0);
        PushSmall(explicitCurves, 0);
        PushOperator(explicitCurves, OpRMoveTo);
        for (const int value : firstCurve)
        {
            PushInt16(explicitCurves, value);
        }
        PushOperator(explicitCurves, OpRRCurveTo);
        for (const int value : secondCurve)
        {
            PushInt16(explicitCurves, value);
        }
        PushOperator(explicitCurves, OpRRCurveTo);
        PushOperator(explicitCurves, OpEndChar);

        const CharstringContext context = EmptyContext();
        CharstringResult viaFlex;
        CharstringResult viaCurves;
        if (!Expect(
                RunType2Charstring(flexed, context, viaFlex) &&
                    RunType2Charstring(explicitCurves, context, viaCurves),
                "both spellings run"))
        {
            return false;
        }

        const std::vector<OutlinePoint> flexPoints = EndPoints(viaFlex.outline);
        const std::vector<OutlinePoint> curvePoints = EndPoints(viaCurves.outline);
        if (!Expect(
                flexPoints.size() == curvePoints.size(),
                "the flex form draws as many points as the curves it stands for"))
        {
            return false;
        }
        bool same = true;
        for (std::size_t index = 0; index < flexPoints.size(); ++index)
        {
            if (!Near(flexPoints[index].x, curvePoints[index].x) ||
                !Near(flexPoints[index].y, curvePoints[index].y))
            {
                std::cerr << "  " << what << " point " << index << ": ("
                          << flexPoints[index].x << "," << flexPoints[index].y << ") from the "
                          << "flex form but (" << curvePoints[index].x << ","
                          << curvePoints[index].y << ") from two curves\n";
                same = false;
            }
        }
        std::cout << "  " << what << ": " << flexPoints.size() << " points, "
                  << (same ? "matching" : "DIFFERENT") << "\n";
        return Expect(same, "and puts them in the same places");
    }

    [[nodiscard]] bool CheckRemainingFlexForms()
    {
        // flex: 좌표 열둘과 판정 기준 하나. 생략이 없으므로 두 곡선과 그대로 같아야 한다.
        bool passed = CheckFlexFormAgainstTwoCurves(
            EscapeFlex,
            { 10, 5, 10, 15, 10, 5, 10, -5, 10, -15, 10, -5, 50 },
            { 10, 5, 10, 15, 10, 5 },
            { 10, -5, 10, -15, 10, -5 },
            "flex");

        // hflex1: dx1 dy1 dx2 dy2 dx3 dx4 dx5 dy5 dx6. 마지막 세로는 dy1+dy2+dy5를 되돌린다.
        constexpr int dy1 = 5;
        constexpr int dy2 = 15;
        constexpr int dy5 = -10;
        passed &= CheckFlexFormAgainstTwoCurves(
            EscapeHFlex1,
            { 10, dy1, 10, dy2, 10, 10, 10, dy5, 10 },
            { 10, dy1, 10, dy2, 10, 0 },
            { 10, 0, 10, dy5, 10, -(dy1 + dy2 + dy5) },
            "hflex1");

        // flex1: 가로로 더 움직였으므로 마지막 값은 dx6이고 세로는 시작 높이로 되돌아간다.
        constexpr int sumY = 5 + 15 + 5 + (-5) + (-15);
        passed &= CheckFlexFormAgainstTwoCurves(
            EscapeFlex1,
            { 20, 5, 20, 15, 20, 5, 20, -5, 20, -15, 20 },
            { 20, 5, 20, 15, 20, 5 },
            { 20, -5, 20, -15, 20, -sumY },
            "flex1 (horizontal)");
        return passed;
    }

    /// <summary>폭은 첫 스택 비우는 연산자에 선택적으로 실린다. 실리면 nominalWidthX로부터의 차다.</summary>
    [[nodiscard]] bool CheckWidthIsReadFromTheFirstOperator()
    {
        CharstringContext context = EmptyContext();
        context.defaultWidthX = 500.0f;
        context.nominalWidthX = 400.0f;

        // 폭 없이: rmoveto가 인자 둘을 받으므로 짝이 맞고, 폭은 기본값이다.
        std::vector<std::byte> withoutWidth;
        PushSmall(withoutWidth, 10);
        PushSmall(withoutWidth, 20);
        PushOperator(withoutWidth, OpRMoveTo);
        PushOperator(withoutWidth, OpEndChar);

        // 폭 있이: 인자가 하나 더 있으면 맨 앞이 폭이다. 120이면 400+120=520.
        std::vector<std::byte> withWidth;
        PushInt16(withWidth, 120);
        PushSmall(withWidth, 10);
        PushSmall(withWidth, 20);
        PushOperator(withWidth, OpRMoveTo);
        PushOperator(withWidth, OpEndChar);

        CharstringResult plain;
        CharstringResult carried;
        if (!Expect(
                RunType2Charstring(withoutWidth, context, plain) &&
                    RunType2Charstring(withWidth, context, carried),
                "both charstrings run"))
        {
            return false;
        }
        std::cout << "  width: default " << plain.advanceWidth << " (500 expected), carried "
                  << carried.advanceWidth << " (520 expected)\n";
        bool passed = Expect(
            Near(plain.advanceWidth, 500.0f),
            "a charstring with no width takes the Private DICT's default");
        passed &= Expect(
            Near(carried.advanceWidth, 520.0f),
            "and one that carries a width reads it against the nominal value");
        return passed;
    }
}

bool RunType2CharstringTests()
{
    bool passed = CheckHintMaskCountsImplicitVerticalStems();
    passed &= CheckFlexMatchesTwoCurves();
    passed &= CheckRemainingFlexForms();
    passed &= CheckWidthIsReadFromTheFirstOperator();
    return passed;
}

static const TestSupport::Registration gType2CharstringTests{
    "RenderCache", "type 2 charstring tests should pass", RunType2CharstringTests };
