#pragma once

/// <summary>시작 대화상자의 선택지가 백엔드 레지스트리와 같은지 검증한다.</summary>
[[nodiscard]] bool RunGraphicsBackendChoiceListTests();

/// <summary>대화상자의 결과와 명령줄 스위치가 프로젝트 설정으로 바뀌는 규칙을 검증한다.</summary>
[[nodiscard]] bool RunGraphicsBackendChoiceApplyTests();

/// <summary>어떤 설정 값이 대화상자를 부르고 어떤 값이 그대로 쓰이는지 검증한다.</summary>
[[nodiscard]] bool RunGraphicsBackendSettingPolicyTests();
