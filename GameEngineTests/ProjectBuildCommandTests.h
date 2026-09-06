#pragma once

/// <summary>
/// 에디터의 Build 버튼이 cmake에게 무엇을 시키는지다. 명령을 만드는 부분만 보므로 창도
/// 빌드도 필요 없고, 실제로 컴파일이 도는지가 아니라 <b>무엇을 어디에 대고 시키는지</b>가
/// 여기서 고정된다 — 그 자리가 틀리면 빌드는 성공하면서 엉뚱한 트리를 건드린다.
/// </summary>
[[nodiscard]] bool RunProjectBuildCommandTests();
