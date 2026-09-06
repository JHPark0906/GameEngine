#pragma once

/// <summary>
/// 주인이 사라진 사이드카를 두 단계로 정리하는 길을 고정한다. 가장 중요한 것은 <b>손대지 않는
/// 경우</b>다: 장면이 그 정체성을 가리키고 있으면 그것은 고아가 아니라 파일이 없는 에셋이고,
/// 그 사이드카가 무엇을 잃었는지 아는 마지막 자리다.
/// </summary>
[[nodiscard]] bool RunOrphanSidecarTests();

/// <summary>
/// 옆으로 치워 둔 사이드카의 주인이 돌아오면 이름과 정체성이 함께 돌아오는지 확인한다. 이것이
/// 없으면 치우는 일 자체가 정체성을 잃게 만들어, 애초에 치우지 않느니만 못하다.
/// </summary>
[[nodiscard]] bool RunSetAsideSidecarReturnTests();

/// <summary>
/// 파일이 사라진 에셋도 인스펙터가 사람이 읽을 수 있게 표시하는지 확인한다.
/// 해석되지 않은 GUID 대신 남아 있는 사이드카의 이름으로 잃어버린 파일을 식별한다.
/// </summary>
[[nodiscard]] bool RunMissingAssetDisplayTests();

/// <summary>
/// 사이드카에 이미 적힌 정체성을 발급이 덮어쓰지 않는지 확인한다. 「정체성이 없다」는 판단은
/// 데이터베이스가 하는데 그것은 오래됐을 수 있고 — 사이드카가 방금 제자리로 돌아온 순간이
/// 그렇다 — 그때 덮어쓰면 그 정체성을 가리키던 참조가 전부 끊긴다.
/// </summary>
[[nodiscard]] bool RunSidecarIdentityIsNeverOverwrittenTests();
