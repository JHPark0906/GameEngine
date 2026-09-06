#pragma once

/// <summary>
/// 화면 UI 컴포넌트가 사각형을 요구한다고 선언했는지, 그리고 사각형이 필요 없는 것들은
/// 요구하지 않는지 확인한다. 월드 스프라이트에까지 사각형이 붙으면 그것은 고침이 아니라
/// 새로운 결함이다.
/// </summary>
[[nodiscard]] bool RunComponentRequirementTests();

/// <summary>
/// 에디터가 컴포넌트를 붙일 때 요구 컴포넌트도 함께 붙이고, 되돌리기가 둘 다 걷는지
/// 확인한다. 되돌리기가 절반만 되돌리면 사람이 놓지 않은 컴포넌트가 남는다.
/// </summary>
[[nodiscard]] bool RunAddComponentRequirementTests();

/// <summary>
/// 사각형은 있는데 Canvas 조상이 없는 오브젝트를 장면 적재가 경고하는지, 그리고 정상인
/// 장면에는 경고하지 않는지 확인한다. 경고가 늘 나오면 아무도 읽지 않게 된다.
/// </summary>
[[nodiscard]] bool RunCanvaslessScreenUiWarningTests();
