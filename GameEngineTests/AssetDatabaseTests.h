#pragma once

[[nodiscard]] bool RunAssetDatabaseTests();
[[nodiscard]] bool RunAssetImporterTests();
[[nodiscard]] bool RunAssetResidencyTests();
[[nodiscard]] bool RunModelInstantiationTests();
[[nodiscard]] bool RunAssetReferenceTests();
[[nodiscard]] bool RunResourceIdTests();

/// <summary>샘플 콘텐츠가 쓰는 시트 모양 — 부분만 쓰는 격자와 비정수 셀 — 을 고정한다.</summary>
[[nodiscard]] bool RunSpriteSheetTests();

/// <summary>
/// 개명 전 이름의 사이드카가 여전히 읽히고, 그 사실이 어느 파일을 무엇으로 바꿔야 하는지까지
/// 로그에 남는지 확인한다. 저장소 밖 프로젝트가 한 번에 개명될 수 없어 이 전환기가 있다.
/// </summary>
[[nodiscard]] bool RunLegacySidecarTests();

/// <summary>
/// 재스캔이 데이터베이스를 갈아 끼울 때, 정체성과 내용이 그대로인 에셋의 상주 페이로드를 새
/// 데이터베이스가 물려받는지 확인한다. 바뀐 파일은 다시 읽혀야 하고, 옮겨진 파일은 물려받아야
/// 한다 — 「이동은 정체성을 지킨다」가 페이로드에서도 참이려면 그렇다.
/// </summary>
[[nodiscard]] bool RunAssetRescanInheritTests();

/// <summary>
/// 사이드카 GUID가 조회 키를 정하고, GUID가 없으면 경로로 폴백하는지 확인한다.
/// 참조 문자열은 경로와 정체성 두 형식을 읽어야 한다. GUID 기반 키는 이동해도 유지되고,
/// 경로 기반 키의 변화는 GPU 페이로드 정체성을 잘못 재사용하지 않아야 한다.
/// </summary>
[[nodiscard]] bool RunAssetGuidTests();

/// <summary>
/// 무엇을 「옮겨진 에셋」으로 볼지의 규칙을 고정한다. 판정이 틀렸을 때의 대가가 비대칭이라 —
/// 못 옮기면 참조가 끊겨 눈에 띄지만 잘못 옮기면 참조가 조용히 다른 것을 가리킨다 — 거절하는
/// 쪽의 조건들이 시험의 대부분이다.
/// </summary>
[[nodiscard]] bool RunAssetMoveMatchingTests();
