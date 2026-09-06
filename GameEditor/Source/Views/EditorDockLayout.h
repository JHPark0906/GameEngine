#pragma once

// editor-layer: 2 (Views)

#include <array>
#include <cstddef>

#include "UI/UIContext.h"

#include "Rules/EditorToolbarLayout.h"

namespace GameEditor
{

/// <summary>좌측 컬럼이 세로로 나뉘는 자리 수다: 계층, 인스펙터, 콘텐츠 브라우저.</summary>
inline constexpr std::size_t LeftDockSlotCount = 3;

/// <summary>중앙 컬럼이 세로로 나뉘는 자리 수다: 씬 뷰, 콘솔.</summary>
inline constexpr std::size_t CenterDockSlotCount = 2;

/// <summary>우측 컬럼이 세로로 나뉘는 자리 수다: 게임 뷰, 타일 팔레트.</summary>
inline constexpr std::size_t RightDockSlotCount = 2;

/// <summary>
/// 도킹 슬롯의 개수다. 세 컬럼이 나뉜 자리를 모두 더한 값이며, 그 합이 곧 정의다.
///
/// 숫자를 따로 적지 않는 이유는 이 값이 혼자 정해지는 값이 아니기 때문이다: 컬럼 하나를
/// 더 쪼개면 슬롯이 늘고, 늘어난 슬롯은 <c>ComputeDockSlotRects</c>가 돌려주는 사각형의
/// 개수이자 셸이 담는 패널의 개수다. 합으로 적어 두면 그 셋이 함께 움직인다.
/// </summary>
inline constexpr std::size_t DockSlotCount =
    LeftDockSlotCount + CenterDockSlotCount + RightDockSlotCount;

/// <summary>
/// 창 하나를 도킹 슬롯 일곱 자리로 나누는 비율이다.
///
/// 값들이 그리는 코드 안에 상수로 흩어져 있으면, 그것이 왜 그 값인지 적을 곳도 다른 값이었으면
/// 어땠을지 재 볼 방법도 없다. 한자리에 모은 이유가 그것이다: 슬롯 사각형은 창 크기만으로
/// 정해지는 순수한 계산이라 창 없이 시험할 수 있고, 시험할 수 있으면 "인스펙터가 몇 줄을
/// 담는가" 같은 질문에 눈이 아니라 산술로 답할 수 있다.
/// </summary>
struct DockSlotMetrics
{
    /// <summary>
    /// 상단 고정 툴바의 높이다. 슬롯들은 그 아래를 나눠 갖는다.
    ///
    /// 기본값은 툴바의 한 줄이며, 실제로 쓰는 값은 툴바가 말한 높이다 — 접히면 두 줄이
    /// 되므로 여기에 숫자를 따로 적어 두면 그 순간 두 값이 갈라진다.
    /// </summary>
    float toolbarHeight = ToolbarLayoutMetrics{}.rowHeight;

    /// <summary>
    /// 좌측 컬럼이 가져가는 너비 비율과 그 하한·상한이다.
    ///
    /// 좌측은 계층·인스펙터·콘텐츠 브라우저가 사는 곳이고, 그중 인스펙터는 너비가 곧 기능이다:
    /// 타일 팔레트가 한 줄에 몇 칸을 놓는지가 패널 너비로 정해지므로, 좁은 좌측은 팔레트를
    /// 세로로 길게 만든다.
    /// </summary>
    float leftFraction = 0.20f;
    float leftMinimum = 200.0f;
    float leftMaximum = 320.0f;

    /// <summary>우측 컬럼(게임 뷰)이 가져가는 너비 비율과 그 하한·상한이다.</summary>
    float rightFraction = 0.30f;
    float rightMinimum = 260.0f;
    float rightMaximum = 560.0f;

    /// <summary>
    /// 좌측 컬럼을 위에서부터 나누는 세로 비율이다: 계층, 인스펙터, 콘텐츠 브라우저. 셋의 합은
    /// 1이어야 하고, 마지막 자리가 남는 만큼을 받는다.
    /// </summary>
    float hierarchyFraction = 0.50f;
    float inspectorFraction = 0.25f;

    /// <summary>중앙 컬럼에서 씬 뷰가 가져가는 세로 비율이다. 나머지가 콘솔이다.</summary>
    float sceneFraction = 0.75f;

    /// <summary>
    /// 우측 컬럼에서 게임 뷰가 가져가는 세로 비율이다. 나머지가 타일 팔레트다.
    ///
    /// 팔레트에 자기 슬롯을 준 이유는 그것이 세로로 긴 목록이기 때문이다. 16x16 타일셋은
    /// 256칸이라, 다른 패널 안에 얹히면 자기 높이를 가질 수 없어 그 패널의 스크롤 안에서 또
    /// 스크롤하게 된다. 슬롯을 가지면 그 겹침이 없고, 무엇보다 사용자가 원하는 자리로 끌어다
    /// 놓을 수 있다 — 패널을 재배치할 수 있게 만들어 둔 것이 이런 경우를 위해서다.
    /// </summary>
    float gameFraction = 0.55f;

    /// <summary>컬럼 하나가 줄어들 수 있는 최소 크기다. 창이 아주 작을 때의 바닥이다.</summary>
    float minimumExtent = 100.0f;
};

/// <summary>
/// 창 크기를 슬롯 일곱 자리의 사각형으로 나눈다. 순서는 좌상·좌중·좌하·중상·중하·우상·우하이며,
/// 그 순서가 곧 슬롯 번호다 — 어느 패널이 어느 슬롯에 놓이는지는 <c>PanelSlotLayout</c>이
/// 따로 답한다.
/// </summary>
/// <param name="width">창의 픽셀 너비다.</param>
/// <param name="height">창의 픽셀 높이다.</param>
/// <param name="metrics">비율과 한계다. 길이는 이미 화면 배율이 곱해진 픽셀이어야 한다.</param>
/// <returns>슬롯 번호 순서의 사각형들이다.</returns>
[[nodiscard]] std::array<GameEngine::UI::UIRect, DockSlotCount> ComputeDockSlotRects(
    float width, float height, const DockSlotMetrics& metrics);

/// <summary>
/// 타일 팔레트가 차지할 높이다. 남은 자리를 다 쓰되, 칸이 그보다 적으면 칸만큼만 쓴다.
///
/// 이 계산이 상수가 아니라 함수인 이유는, 상한을 고정하면 그 상한이 곧 두 번째 스크롤이 되기
/// 때문이다: 16x16 타일셋의 256칸은 아홉 열로 늘어놓아도 29줄이라, 여섯 줄짜리 창에 묶이면 그
/// 창의 스크롤 안에서 또 스크롤해야 한다. 패널을 넓히는 것으로는 풀리지 않는다 — 너비는 열을
/// 늘려 줄 수를 32에서 29로 줄일 뿐이고, 29는 여전히 6이 아니다.
///
/// 팔레트의 높이는 자기 패널을 따라가고, 넘칠 때만 팔레트 자신이 스크롤한다.
/// 별도 높이 상한을 두지 않아 스크롤을 한 겹으로 유지한다.
/// </summary>
/// <param name="availableHeight">패널에서 팔레트 위쪽 줄들을 뺀 나머지 높이다.</param>
/// <param name="totalRows">타일셋이 이 너비에서 차지하는 전체 줄 수다.</param>
/// <param name="cellSize">팔레트 칸 하나의 변 길이다.</param>
/// <returns>팔레트 상자의 높이이며, 최소한 한 줄은 된다.</returns>
[[nodiscard]] float ComputePaletteBoxHeight(
    float availableHeight, int totalRows, float cellSize);

}
