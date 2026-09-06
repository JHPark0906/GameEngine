#pragma once

/// <summary>
/// 열린 프로젝트의 <b>코드</b>가 어디 사는지를 고정한다. 에셋과 달리 프로젝트 파일 기준
/// 상대 경로로 물을 수 있다 — 설정 파일과 에셋을 저장소의 <c>Content/</c>에 두고
/// <c>CMakeLists.txt</c>는 저장소 루트에 두는 모양이 있고, 그때 이 값이 <c>".."</c>이다.
/// </summary>
[[nodiscard]] bool RunProjectSourceRootTests();
