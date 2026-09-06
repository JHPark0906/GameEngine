#pragma once

#include <span>
#include <string_view>

/// <summary>
/// 성능 측정 하네스다: `GameEngineTests --benchmark <프로젝트 콘텐츠 경로>`로 손으로 돌린다.
/// CTest에는 등록되지 않는다 — 시간 단언은 머신 의존이라, 목적은 회귀 방지가 아니라 백로그
/// 후보(월드 행렬 캐싱, 장면 탐색)를 숫자로 판단할 근거다. 실행 방법은 Docs/VALIDATION.md에
/// 있으며, 결과에는 측정한 머신·구성·프로젝트 경로를 함께 기록한다.
/// </summary>
[[nodiscard]] bool RunBenchmarks(std::span<const std::string_view> arguments);

/// <summary>
/// 핫스팟 측정이다: `GameEngineTests --measure <프로젝트 콘텐츠 경로> [에셋 수...]`로 손으로
/// 돌린다. 벤치마크와 같은 이유로 CTest에 등록되지 않으며, 최적화 착수 여부를 판단할 세 지점 —
/// 프로젝트와 장면 열기, 계층 행 재구축, 에디터 뷰의 프레임당 고정비 — 을 잰다.
/// </summary>
[[nodiscard]] bool RunHotspotMeasurements(std::span<const std::string_view> arguments);
