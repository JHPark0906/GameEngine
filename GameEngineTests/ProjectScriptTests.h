#pragma once

/// <summary>
/// 열린 프로젝트에 컴포넌트 하나를 만드는 일이다: 어떤 이름을 받아 주는지, 무엇을 놓는지,
/// 그리고 코드가 없던 프로젝트가 그것으로 빌드되는 프로젝트가 되는지.
/// </summary>
[[nodiscard]] bool RunProjectScriptTests();

/// <summary>
/// 만들어진 프로젝트가 실제로 configure되는지다. 파일을 놓는 것과 그 파일들이 빌드에 들어가는
/// 것은 다른 일이라, 앞의 시험이 초록인 채로 뒤가 깨질 수 있다.
/// </summary>
[[nodiscard]] bool RunProjectScriptConfigureTests();
