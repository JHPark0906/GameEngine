#include "pch.h"
#include "Type2Charstring.h"

#include <array>
#include <cmath>

namespace GameEngine::Text
{

namespace
{
    /// <summary>Type 2가 허용하는 스택 깊이다. 넘으면 파일이 잘못된 것이다.</summary>
    constexpr std::size_t MaximumStack = 48;
    /// <summary>subr이 subr을 부르는 깊이의 한계다. 자기를 부르는 파일을 여기서 끊는다.</summary>
    constexpr unsigned int MaximumCallDepth = 10;

    /// <summary>
    /// 해석 중인 상태다. subr을 부르면 같은 상태 위에서 이어 달린다 — charstring과 subr은
    /// 별개의 프로그램이 아니라 한 프로그램을 나눠 적은 것이다.
    /// </summary>
    struct Interpreter
    {
        const CharstringContext* context = nullptr;
        std::array<float, MaximumStack> stack{};
        std::size_t stackSize = 0;

        float x = 0.0f;
        float y = 0.0f;
        unsigned int stemCount = 0;
        bool widthParsed = false;
        float advanceWidth = 0.0f;

        GlyphOutline* outline = nullptr;
        OutlineContour current;
        bool contourOpen = false;

        void Push(const float value)
        {
            if (stackSize < MaximumStack)
            {
                stack[stackSize++] = value;
            }
        }

        void Clear() { stackSize = 0; }

        /// <summary>
        /// 첫 스택 비우는 연산자에서 폭을 꺼낸다.
        ///
        /// 폭이 실렸는지는 <b>인자가 하나 더 있는지</b>로만 알 수 있다. 그래서 연산자마다
        /// 「짝수여야 한다」거나 「몇 개여야 한다」를 알고 있어야 하고, 그보다 하나 많으면 맨
        /// 앞이 폭이다. 한 번만 일어나는 일이라 그 뒤로는 묻지 않는다.
        /// </summary>
        void TakeWidth(const bool oddMeansWidth)
        {
            if (widthParsed)
            {
                return;
            }
            widthParsed = true;
            if (!oddMeansWidth)
            {
                advanceWidth = context->defaultWidthX;
                return;
            }
            advanceWidth = context->nominalWidthX + stack[0];
            // 폭을 걷어내고 나머지를 앞으로 당긴다. 뒤의 코드가 폭을 모르게 하려는 것이다.
            for (std::size_t index = 1; index < stackSize; ++index)
            {
                stack[index - 1] = stack[index];
            }
            --stackSize;
        }

        void TakeWidthIfCountExceeds(const std::size_t expected)
        {
            TakeWidth(!widthParsed && stackSize > expected);
        }

        void TakeWidthIfOdd()
        {
            TakeWidth(!widthParsed && (stackSize % 2) == 1);
        }

        void CloseContour()
        {
            if (!contourOpen)
            {
                return;
            }
            if (!current.segments.empty())
            {
                // 마지막 점이 시작점으로 돌아오지 않았으면 직선으로 닫는다. Type 2의 고리는
                // 언제나 닫힌 것으로 친다.
                const OutlinePoint last = current.segments.back().end;
                if (std::abs(last.x - current.start.x) > 0.0001f ||
                    std::abs(last.y - current.start.y) > 0.0001f)
                {
                    current.segments.push_back(PromoteLine(last, current.start));
                }
                outline->contours.push_back(std::move(current));
            }
            current = OutlineContour{};
            contourOpen = false;
        }

        void MoveTo(const float dx, const float dy)
        {
            CloseContour();
            x += dx;
            y += dy;
            current.start = OutlinePoint{ x, y };
            contourOpen = true;
        }

        void LineTo(const float dx, const float dy)
        {
            const OutlinePoint from{ x, y };
            x += dx;
            y += dy;
            if (contourOpen)
            {
                current.segments.push_back(PromoteLine(from, OutlinePoint{ x, y }));
            }
        }

        void CurveTo(
            const float dx1, const float dy1, const float dx2, const float dy2,
            const float dx3, const float dy3)
        {
            const OutlinePoint control1{ x + dx1, y + dy1 };
            const OutlinePoint control2{ control1.x + dx2, control1.y + dy2 };
            x = control2.x + dx3;
            y = control2.y + dy3;
            if (contourOpen)
            {
                current.segments.push_back(
                    OutlineSegment{ control1, control2, OutlinePoint{ x, y } });
            }
        }
    };

    /// <summary>subr 번호에 더하는 값이다. 목록 크기로 정해지며, 음수 번호를 쓰기 위한 것이다.</summary>
    [[nodiscard]] int SubroutineBias(const std::size_t count)
    {
        if (count < 1240)
        {
            return 107;
        }
        return count < 33900 ? 1131 : 32768;
    }

    [[nodiscard]] bool Execute(
        std::span<const std::byte> code, Interpreter& state, unsigned int depth);

    /// <summary>flex 사인방을 두 곡선으로 편다. 넷의 차이는 무엇을 생략하느냐뿐이다.</summary>
    void RunFlex(Interpreter& state, const unsigned char escape)
    {
        const float* const values = state.stack.data();
        switch (escape)
        {
        case 35:  // flex: 열세 개. 마지막은 판정 기준이라 그림에 쓰지 않는다.
            state.CurveTo(values[0], values[1], values[2], values[3], values[4], values[5]);
            state.CurveTo(values[6], values[7], values[8], values[9], values[10], values[11]);
            break;
        case 34:
        {
            // hflex: 일곱 개. 세로 움직임은 dy2 하나뿐이고, 둘째 곡선이 처음 높이로 돌아온다.
            const float dy2 = values[2];
            state.CurveTo(values[0], 0.0f, values[1], dy2, values[3], 0.0f);
            state.CurveTo(values[4], 0.0f, values[5], -dy2, values[6], 0.0f);
            break;
        }
        case 36:
        {
            // hflex1: 아홉 개 — dx1 dy1 dx2 dy2 dx3 dx4 dx5 dy5 dx6.
            // 첫 곡선은 세로로 움직이고 세 번째 점에서 평평해진다. 둘째 곡선의 마지막 세로
            // 움직임은 <b>적히지 않는다</b>: 전체가 시작 높이로 돌아와야 하므로 앞의 세 세로
            // 움직임을 되돌리는 값이고, 그것을 우리가 계산한다.
            const float dy1 = values[1];
            const float dy2 = values[3];
            const float dy5 = values[7];
            state.CurveTo(values[0], dy1, values[2], dy2, values[4], 0.0f);
            state.CurveTo(values[5], 0.0f, values[6], dy5, values[8], -(dy1 + dy2 + dy5));
            break;
        }
        case 37:
        {
            // flex1: 열한 개 — 좌표 열 개와 마지막 값 하나. 그 하나가 가로인지 세로인지는
            // <b>지금까지 어느 쪽으로 더 움직였는지</b>로 갈리고, 반대쪽은 시작점으로 되돌아오는
            // 값이 된다. 이 갈림을 놓치면 획이 깨지지 않고 미세하게 휜다.
            float sumX = 0.0f;
            float sumY = 0.0f;
            for (std::size_t index = 0; index < 10; index += 2)
            {
                sumX += values[index];
                sumY += values[index + 1];
            }
            state.CurveTo(values[0], values[1], values[2], values[3], values[4], values[5]);
            if (std::abs(sumX) > std::abs(sumY))
            {
                state.CurveTo(values[6], values[7], values[8], values[9], values[10], -sumY);
            }
            else
            {
                state.CurveTo(values[6], values[7], values[8], values[9], -sumX, values[10]);
            }
            break;
        }
        default:
            break;
        }
        state.Clear();
    }

    bool Execute(
        const std::span<const std::byte> code, Interpreter& state, const unsigned int depth)
    {
        if (depth > MaximumCallDepth)
        {
            return false;
        }

        std::size_t cursor = 0;
        while (cursor < code.size())
        {
            const auto byte = static_cast<unsigned char>(code[cursor]);

            // 32 이상은 값이다. 인코딩이 넷으로 갈린다.
            if (byte >= 32 || byte == 28)
            {
                if (byte == 28)
                {
                    if (cursor + 3 > code.size())
                    {
                        return false;
                    }
                    const auto high = static_cast<unsigned char>(code[cursor + 1]);
                    const auto low = static_cast<unsigned char>(code[cursor + 2]);
                    state.Push(static_cast<float>(
                        static_cast<std::int16_t>((high << 8) | low)));
                    cursor += 3;
                }
                else if (byte <= 246)
                {
                    state.Push(static_cast<float>(static_cast<int>(byte) - 139));
                    ++cursor;
                }
                else if (byte <= 250)
                {
                    if (cursor + 2 > code.size())
                    {
                        return false;
                    }
                    const auto low = static_cast<unsigned char>(code[cursor + 1]);
                    state.Push(static_cast<float>((byte - 247) * 256 + low + 108));
                    cursor += 2;
                }
                else if (byte <= 254)
                {
                    if (cursor + 2 > code.size())
                    {
                        return false;
                    }
                    const auto low = static_cast<unsigned char>(code[cursor + 1]);
                    state.Push(-static_cast<float>((byte - 251) * 256 + low + 108));
                    cursor += 2;
                }
                else
                {
                    // 255는 16.16 고정소수점이다.
                    if (cursor + 5 > code.size())
                    {
                        return false;
                    }
                    std::int32_t fixed = 0;
                    for (std::size_t index = 1; index <= 4; ++index)
                    {
                        fixed = (fixed << 8) |
                            static_cast<std::int32_t>(static_cast<unsigned char>(code[cursor + index]));
                    }
                    state.Push(static_cast<float>(fixed) / 65536.0f);
                    cursor += 5;
                }
                continue;
            }

            ++cursor;
            switch (byte)
            {
            case 1:   // hstem
            case 3:   // vstem
            case 18:  // hstemhm
            case 23:  // vstemhm
                state.TakeWidthIfOdd();
                state.stemCount += static_cast<unsigned int>(state.stackSize / 2);
                state.Clear();
                break;

            case 19:  // hintmask
            case 20:  // cntrmask
            {
                // 🪤 여기가 함정이다. 마스크 앞에 남아 있는 값들은 <b>적히지 않은 vstem</b>이며
                // 스템 수에 들어간다. 그 수가 마스크의 바이트 폭을 정하므로, 이것을 빼먹으면
                // 마스크를 덜 건너뛰고 그 다음 바이트부터 명령이 통째로 밀린다.
                state.TakeWidthIfOdd();
                state.stemCount += static_cast<unsigned int>(state.stackSize / 2);
                state.Clear();
                const std::size_t maskBytes = (state.stemCount + 7) / 8;
                if (cursor + maskBytes > code.size())
                {
                    return false;
                }
                cursor += maskBytes;
                break;
            }

            case 21:  // rmoveto
                state.TakeWidthIfCountExceeds(2);
                if (state.stackSize >= 2)
                {
                    state.MoveTo(state.stack[0], state.stack[1]);
                }
                state.Clear();
                break;

            case 22:  // hmoveto
                state.TakeWidthIfCountExceeds(1);
                if (state.stackSize >= 1)
                {
                    state.MoveTo(state.stack[0], 0.0f);
                }
                state.Clear();
                break;

            case 4:  // vmoveto
                state.TakeWidthIfCountExceeds(1);
                if (state.stackSize >= 1)
                {
                    state.MoveTo(0.0f, state.stack[0]);
                }
                state.Clear();
                break;

            case 5:  // rlineto
                for (std::size_t index = 0; index + 1 < state.stackSize; index += 2)
                {
                    state.LineTo(state.stack[index], state.stack[index + 1]);
                }
                state.Clear();
                break;

            case 6:  // hlineto
            case 7:  // vlineto
            {
                // 둘은 가로와 세로를 <b>번갈아</b> 그린다. 시작 방향만 다르다.
                bool horizontal = byte == 6;
                for (std::size_t index = 0; index < state.stackSize; ++index)
                {
                    if (horizontal)
                    {
                        state.LineTo(state.stack[index], 0.0f);
                    }
                    else
                    {
                        state.LineTo(0.0f, state.stack[index]);
                    }
                    horizontal = !horizontal;
                }
                state.Clear();
                break;
            }

            case 8:  // rrcurveto
                for (std::size_t index = 0; index + 5 < state.stackSize; index += 6)
                {
                    state.CurveTo(
                        state.stack[index], state.stack[index + 1], state.stack[index + 2],
                        state.stack[index + 3], state.stack[index + 4], state.stack[index + 5]);
                }
                state.Clear();
                break;

            case 24:  // rcurveline: 곡선 여럿 뒤에 직선 하나.
            {
                std::size_t index = 0;
                while (state.stackSize - index >= 8)
                {
                    state.CurveTo(
                        state.stack[index], state.stack[index + 1], state.stack[index + 2],
                        state.stack[index + 3], state.stack[index + 4], state.stack[index + 5]);
                    index += 6;
                }
                if (index + 1 < state.stackSize)
                {
                    state.LineTo(state.stack[index], state.stack[index + 1]);
                }
                state.Clear();
                break;
            }

            case 25:  // rlinecurve: 직선 여럿 뒤에 곡선 하나.
            {
                std::size_t index = 0;
                while (state.stackSize - index >= 8)
                {
                    state.LineTo(state.stack[index], state.stack[index + 1]);
                    index += 2;
                }
                if (index + 5 < state.stackSize)
                {
                    state.CurveTo(
                        state.stack[index], state.stack[index + 1], state.stack[index + 2],
                        state.stack[index + 3], state.stack[index + 4], state.stack[index + 5]);
                }
                state.Clear();
                break;
            }

            case 26:  // vvcurveto
            case 27:  // hhcurveto
            {
                // 첫 곡선에만 축을 가로지르는 값이 하나 붙을 수 있다. 홀수면 그것이 있는 것이다.
                std::size_t index = 0;
                float crossFirst = 0.0f;
                if ((state.stackSize % 4) == 1)
                {
                    crossFirst = state.stack[0];
                    index = 1;
                }
                for (; index + 3 < state.stackSize; index += 4)
                {
                    if (byte == 26)
                    {
                        state.CurveTo(
                            crossFirst, state.stack[index], state.stack[index + 1],
                            state.stack[index + 2], 0.0f, state.stack[index + 3]);
                    }
                    else
                    {
                        state.CurveTo(
                            state.stack[index], crossFirst, state.stack[index + 1],
                            state.stack[index + 2], state.stack[index + 3], 0.0f);
                    }
                    crossFirst = 0.0f;
                }
                state.Clear();
                break;
            }

            case 30:  // vhcurveto
            case 31:  // hvcurveto
            {
                // 곡선마다 시작과 끝의 방향이 번갈아 바뀐다. 마지막 곡선에만 남는 값 하나가
                // 붙을 수 있고, 그것이 원래 생략됐을 좌표다.
                bool horizontal = byte == 31;
                std::size_t index = 0;
                while (index + 3 < state.stackSize)
                {
                    const bool last = state.stackSize - index == 5;
                    const float extra = last ? state.stack[index + 4] : 0.0f;
                    if (horizontal)
                    {
                        state.CurveTo(
                            state.stack[index], 0.0f, state.stack[index + 1],
                            state.stack[index + 2], extra, state.stack[index + 3]);
                    }
                    else
                    {
                        state.CurveTo(
                            0.0f, state.stack[index], state.stack[index + 1],
                            state.stack[index + 2], state.stack[index + 3], extra);
                    }
                    horizontal = !horizontal;
                    index += 4;
                }
                state.Clear();
                break;
            }

            case 10:  // callsubr
            case 29:  // callgsubr
            {
                if (state.stackSize == 0)
                {
                    return false;
                }
                const std::span<const std::span<const std::byte>> subroutines = byte == 10
                    ? state.context->localSubroutines
                    : state.context->globalSubroutines;
                const int index = static_cast<int>(state.stack[--state.stackSize]) +
                    SubroutineBias(subroutines.size());
                if (index < 0 || static_cast<std::size_t>(index) >= subroutines.size())
                {
                    return false;
                }
                if (!Execute(subroutines[static_cast<std::size_t>(index)], state, depth + 1))
                {
                    return false;
                }
                break;
            }

            case 11:  // return
                return true;

            case 14:  // endchar
                state.TakeWidthIfCountExceeds(0);
                state.CloseContour();
                return true;

            case 12:
            {
                if (cursor >= code.size())
                {
                    return false;
                }
                const auto escape = static_cast<unsigned char>(code[cursor]);
                ++cursor;
                if (escape == 34 || escape == 35 || escape == 36 || escape == 37)
                {
                    RunFlex(state, escape);
                }
                else
                {
                    // 산술과 저장 연산자들은 동봉 폰트에 쓰이지 않는다. 값을 지어내는 대신
                    // 스택만 비워, 모르는 것을 아는 척하지 않는다.
                    state.Clear();
                }
                break;
            }

            default:
                state.Clear();
                break;
            }
        }
        return true;
    }
}

bool RunType2Charstring(
    const std::span<const std::byte> charstring,
    const CharstringContext& context,
    CharstringResult& result)
{
    result = CharstringResult{};

    Interpreter state;
    state.context = &context;
    state.outline = &result.outline;
    state.advanceWidth = context.defaultWidthX;

    if (!Execute(charstring, state, 0))
    {
        return false;
    }
    // endchar 없이 끝난 charstring도 고리는 닫아 준다.
    state.CloseContour();

    result.advanceWidth = state.advanceWidth;
    result.stemCount = state.stemCount;
    return true;
}

}
