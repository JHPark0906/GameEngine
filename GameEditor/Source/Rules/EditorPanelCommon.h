#pragma once

// editor-layer: 0 (Rules)

#include <filesystem>
#include <string>

#include "Core/TextEncoding.h"
#include "Math/Color.h"

namespace GameEditor
{

// 패널들이 공유하는 어두운 팔레트. 씬 뷰 배경(0.16)과 게임의 화면이 UI에서 떠 보이도록 반 단계
// 어둡다.
inline constexpr GameEngine::Math::Color PanelColor{ 0.13f, 0.14f, 0.15f, 1.0f };
/// <summary>
/// 패널 머리줄과 툴바 띠의 색이다.
/// <para>
/// ⚠ 이 색 위에 비활성 버튼이 선다. <c>Button</c>의 <c>disabled</c> 기본값
/// (0.040, 0.045, 0.050)은 이 띠가 <b>가장 어두워지는 경우</b>를 기준으로 잡혀 있고,
/// 그 수(29, 31, 35)가 GameEngineTests의 <c>measuredToolbarBand</c> 상수에 요구로
/// 박혀 있다. 이 색을 바꾸거나 띠의 그림을 갈면 그 상수도 함께 고쳐야 한다 —
/// 안 고치면 시험이 화면에 없는 배경을 지키게 되고, 실제 배경 위에서 버튼이 안 보이는
/// 것은 아무도 못 잡는다.
/// </para>
/// </summary>
inline constexpr GameEngine::Math::Color HeaderColor{ 0.17f, 0.18f, 0.20f, 1.0f };
inline constexpr GameEngine::Math::Color TextColor{ 0.86f, 0.87f, 0.88f, 1.0f };
inline constexpr GameEngine::Math::Color DimTextColor{ 0.55f, 0.57f, 0.60f, 1.0f };
inline constexpr GameEngine::Math::Color WarningColor{ 0.90f, 0.80f, 0.40f, 1.0f };
inline constexpr GameEngine::Math::Color ErrorColor{ 0.94f, 0.45f, 0.45f, 1.0f };
/// <summary>드래그 중인 객체나 패널을 놓을 수 있는 자리의 강조색이다.</summary>
inline constexpr GameEngine::Math::Color DropTargetColor{ 0.20f, 0.42f, 0.30f, 1.0f };

/// <summary>
/// 게임이 입력을 쥐고 있는 동안 게임 뷰를 두르는 색이다. 보이는 신호가 없으면 사람은 "키가
/// 안 먹는다"와 "아직 포커스를 안 잡았다"를 가를 수 없다.
/// </summary>
inline constexpr GameEngine::Math::Color PlayFocusColor{ 0.36f, 0.62f, 0.94f, 1.0f };

/// <summary>그 테두리의 굵기다. 논리 픽셀이며 배율은 그리는 쪽이 곱한다.</summary>
inline constexpr float PlayFocusBorderThickness = 2.0f;

// 아래 길이는 96 DPI 기준의 논리 픽셀이다. 셸의 S()가 화면 배율을 곱한다.
inline constexpr float HeaderHeight = 22.0f;
inline constexpr float RowHeight = 20.0f;
inline constexpr float Padding = 4.0f;
/// <summary>
/// 패널 목록의 한 줄이 쓰는 글자 크기다. 목록 위에 서는 것 — 계층의 저장 질문과 그 버튼 —
/// 도 같은 크기를 써야 한 화면에서 같은 층으로 읽힌다. 화면 배율은 그리는 쪽이 곱한다.
///
/// 에디터의 본문 크기이므로 여기서 한 번 정하면 툴바 레이블과 즉시 모드 위젯까지 따라온다.
/// </summary>
inline constexpr float RowFontSize = 14.0f;

/// <summary>
/// 본문보다 한 단계 작은 글자다. 인스펙터의 속성 이름, 비어 있는 목록의 안내 문구, 콘솔의
/// 로그처럼 읽는 것이 아니라 훑는 것에 쓴다.
///
/// 값이 아니라 <b>본문과의 간격</b>이 이것의 정의다. 본문이 움직이면 이것도 같은 만큼 움직여야
/// 두 계열이 한 화면에서 계속 두 층으로 읽힌다 — 간격이 벌어지면 보조가 작아 보이는 것이 아니라
/// 다른 글꼴로 보인다.
/// </summary>
inline constexpr float SecondaryFontSize = RowFontSize - 1.0f;

/// <summary>
/// 인스펙터에서 속성 이름이 서는 칸의 폭이다. 값 칸들은 남은 자리를 나눠 갖는다.
///
/// 이 폭을 정하는 것은 <b>가장 긴 속성 이름</b>이다. 들어가지 않는 이름은 넘치는 것이 아니라
/// 말줄임으로 잘리므로 화면에서는 조용하고, 이름이 곧 그 줄의 정체성인 칸에서 뒤가 사라지면
/// 무슨 값을 보고 있는지 알 수 없게 된다. "Vertical Alignment"를 NanumSquareNeo 본문
/// 폰트와 보조 글자 크기로 재면 124px이고, 왼쪽 들여쓰기 6px을 더한 130px이 하한이다.
/// 비슷한 길이의 이름을 수용하도록 10px의 여유를 둔다. 시험이 이 관계를 지킨다.
/// </summary>
inline constexpr float InspectorLabelWidth = 140.0f;

/// <summary>
/// 떠 있는 창이 화면 안에 남겨야 하는 가로 길이다. 이만큼은 보여야 제목줄을 다시 잡을 수 있다.
/// </summary>
inline constexpr float MinimumVisibleWindow = 96.0f;

/// <summary>
/// 프로젝트 이름 같은 와이드 문자열을 UI 텍스트로 바꾼다.
///
/// Core의 공통 변환을 사용해 플랫폼 계층 의존과 중복 인코딩 규칙을 피한다.
/// </summary>
[[nodiscard]] inline std::string ToUtf8(const std::wstring& text)
{
    return GameEngine::Core::Utf16ToUtf8(std::u16string(text.begin(), text.end()));
}

}
