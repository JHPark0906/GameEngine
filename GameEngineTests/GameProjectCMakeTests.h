#pragma once

/// <summary>
/// 게임 프로젝트의 컴포넌트가 에디터 안에 실제로 등록되는지다.
///
/// 이 시험이 있는 이유는 그것이 조용히 깨질 수 있기 때문이다. 등록은 프로젝트 자신의 번역
/// 단위에 있는 정적 초기화자가 하는데, 그 오브젝트를 링커가 버려도 빌드는 통과한다 — 없어지는
/// 것은 Play 모드의 컴포넌트뿐이고 아무것도 그것을 말하지 않는다.
/// </summary>
[[nodiscard]] bool RunGameProjectComponentLinkTests();

/// <summary>
/// 게임 프로젝트를 경로로 가리키는 configure가 네 경우에 무엇을 하는지다: 제대로 된 프로젝트,
/// 빈 값, 없는 경로, 그리고 CMakeLists.txt가 없는 디렉터리.
///
/// 실제로 <c>cmake</c>를 돌린다. 이 규칙들은 CMake 파일에 있어서 C++에서 재현할 수 없고,
/// 재현하면 진짜 configure가 아니라 그 재현을 시험하게 된다.
/// </summary>
[[nodiscard]] bool RunGameProjectConfigureTests();
