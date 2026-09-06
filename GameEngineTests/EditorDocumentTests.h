#pragma once

[[nodiscard]] bool RunEditorUnsavedChangeTests();

[[nodiscard]] /// <summary>Play 모드에서 프로젝트 컴포넌트의 Update가 실제로 불리는지 확인한다.</summary>
bool RunEditorPlayModeComponentTests();

bool RunEditorDockLayoutTests();

/// <summary>시작 장면을 못 열었을 때 그 사실이 사람에게 전달되는지 검증한다.</summary>
bool RunEditorMissingSceneReportTests();


/// <summary>
/// 등록된 장면 파일이 없어도 프로젝트 열기를 허용하고 프로젝트 밖을 가리키는 등록은 거절하는지 확인한다.
/// </summary>
bool RunEditorMissingSceneFileTests();
/// <summary>
/// 에디터가 장면 만들기·이름 바꾸기·지우기 명령을 내놓는지 확인한다. 기능이 든 파일이 병합에서
/// 지워질 때 붉어지는 자리다 — 컴파일과 나머지 시험은 그때 아무 말도 하지 않는다.
/// </summary>
[[nodiscard]] bool RunSceneCommandTests();

/// <summary>
/// 프로젝트 디렉터리의 파일 변동이 에셋 데이터베이스에 도달하는지, 그리고 스캔이 실패할 때
/// 기존 데이터베이스가 살아남는지 확인한다. 창 없이 EditorContext만으로 선다.
/// </summary>
[[nodiscard]] bool RunEditorAssetWatchTests();
/// <summary>유지 모드 툴바가 도킹 계산이 비워 둔 그 띠를 정확히 덮는지 확인한다.</summary>
[[nodiscard]] bool RunEditorToolbarStripTests();
/// <summary>
/// 본문·보조 글자 한 줄이 목록 한 줄의 높이 안에 들어가는지 확인한다. 글자 크기와 줄 높이는
/// 따로 적혀 있어 한쪽만 움직이면 조용히 어긋난다.
/// </summary>
[[nodiscard]] bool RunEditorRowFontFitTests();

/// <summary>
/// 떠 있는 창이 화면 밖으로 밀려나 다시 잡을 수 없게 되지 않는지 확인한다.
/// </summary>
[[nodiscard]] bool RunEditorFloatingWindowClampTests();

/// <summary>
/// 사이드카가 없는 에셋에 기본 사이드카가 놓이는지, 그리고 이미 있는 파일은 — 옛 이름의 것도 —
/// 건드려지지 않는지 확인한다. 두 번째 스캔이 아무것도 쓰지 않는 것까지 고정하므로, 감시자가
/// 자기가 만든 파일을 보고 다시 도는 되먹임이 여기서 걸린다.
/// </summary>
[[nodiscard]] bool RunSidecarWriterTests();

/// <summary>
/// 사람이 탐색기에서 파일을 옮기거나 이름을 바꾼 뒤 다시 읽었을 때, 정체성이 따라오는지와
/// 따라오지 <b>않아야</b> 하는 경우들을 고정한다. 옮기지 못하면 그 에셋을 가리키던 모든 참조가
/// 끊기고, 잘못 옮기면 그 참조들이 조용히 다른 에셋을 가리킨다.
/// </summary>
[[nodiscard]] bool RunAssetMoveTests();

/// <summary>
/// 장면이 에셋을 경로로 가리키던 것을 정체성으로 옮기는, 되돌리기 어려운 유일한 단계를 고정한다.
/// 승인·거부 두 답과, 막는 조건, 그리고 이관이 실제로 무엇을 얻었는지 — 이관 뒤에는 에셋을 옮겨도
/// 참조가 살아 있다 — 를 창 없이 잰다.
/// </summary>
[[nodiscard]] bool RunSceneReferenceMigrationTests();
