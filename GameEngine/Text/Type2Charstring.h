#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "GlyphOutline.h"

namespace GameEngine::Text
{

/// <summary>
/// Type 2 charstring 하나를 해석하는 데 필요한, 그 charstring 바깥의 것들이다.
///
/// subr 목록이 둘인 것은 CFF의 구조 그대로다: 전역은 폰트 하나에 한 벌이고, 지역은 그 글리프가
/// 속한 Private DICT의 것이다. <b>CID 키 방식 폰트에서는 지역 목록이 글리프마다 다르다</b> —
/// 어느 목록을 줄지는 부르는 쪽이 FDSelect로 정해서 넘긴다.
/// </summary>
struct CharstringContext
{
    /// <summary>전역 subr들이다. 각 원소가 charstring 하나의 바이트다.</summary>
    std::span<const std::span<const std::byte>> globalSubroutines;
    /// <summary>이 글리프가 쓰는 지역 subr들이다.</summary>
    std::span<const std::span<const std::byte>> localSubroutines;

    /// <summary>폭이 charstring에 실리지 않았을 때 쓰는 값이다. Private DICT에서 온다.</summary>
    float defaultWidthX = 0.0f;
    /// <summary>charstring에 실린 폭은 이 값으로부터의 차이다. Private DICT에서 온다.</summary>
    float nominalWidthX = 0.0f;
};

/// <summary>charstring 하나를 해석한 결과다.</summary>
struct CharstringResult
{
    GlyphOutline outline;
    /// <summary>이 글리프의 진행폭이다. hmtx가 말하는 것과 같아야 한다.</summary>
    float advanceWidth = 0.0f;
    /// <summary>세운 스템의 수다. <c>hintmask</c>의 바이트 폭이 여기서 나온다.</summary>
    unsigned int stemCount = 0;
};

/// <summary>
/// Type 2 charstring을 윤곽선으로 해석한다.
///
/// 두 자리가 이 해석기에서 가장 자주, 그리고 가장 조용히 틀린다.
///
/// 하나는 <c>hintmask</c>다. 그 뒤에 오는 마스크 바이트의 개수가 <b>그때까지 세운 스템의
/// 수</b>로 정해지는데, 스템은 <c>hstem</c>/<c>vstem</c>으로만 세워지는 것이 아니다.
/// <c>hintmask</c>가 나오기 전에 스택에 남아 있던 짝수 개의 값은 <b>적히지 않은
/// <c>vstem</c></b>으로 친다. 그 암묵 규칙을 빼면 마스크를 한 바이트 적게 건너뛰고, 그 다음
/// 바이트부터 명령이 통째로 밀린다 — 죽지 않고 엉뚱한 모양이 나온다.
///
/// 다른 하나는 flex 사인방(<c>flex</c>·<c>hflex</c>·<c>hflex1</c>·<c>flex1</c>)이다. 넷은
/// 인자의 수와 <b>생략된 좌표를 무엇으로 채우는지</b>가 저마다 다르다. 거의 평평한 곡선 둘을
/// 한 번에 적는 축약이라, 틀리면 글자가 깨지는 대신 획이 미세하게 휜다.
/// </summary>
/// <param name="charstring">해석할 바이트다.</param>
/// <param name="context">subr 목록과 폭 기준값이다.</param>
/// <param name="result">윤곽선과 진행폭을 받는다.</param>
/// <returns>해석했으면 true다.</returns>
[[nodiscard]] bool RunType2Charstring(
    std::span<const std::byte> charstring,
    const CharstringContext& context,
    CharstringResult& result);

}
