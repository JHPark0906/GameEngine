#pragma once

/// <summary>
/// 각 시험 헤더가 선언한 진입점에 자체 등록이 있는지 확인한다.
/// 원본의 등록 수와 실행 중 등록부 크기를 비교해 누락되거나 링커가 제거한 등록 객체를 검출한다.
/// 시험을 정적 라이브러리로 구성하는 경우에도 등록 객체가 최종 실행 파일에 포함되어야 한다.
/// </summary>
[[nodiscard]] bool RunTestRegistrationTests();
