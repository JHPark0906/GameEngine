#pragma once

/// <summary>
/// 페이로드 캐시의 키와 수명을 확인한다. 표를 다시 세울 때 제거되거나 변경된 에셋의
/// 페이로드는 남지 않아야 하며, GPU 리소스 ID의 씨앗은 캐시 구조와 독립적이어야 한다.
/// InheritPayloadsFrom은 GUID와 내용 해시로 짝을 지으므로 이동 시 재사용은 GUID가 있는
/// 에셋에 한정한다. 정체성 없는 에셋은 다시 임포트한다.
/// </summary>
[[nodiscard]] bool RunAssetPayloadKeyTests();
