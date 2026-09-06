#pragma once

/// <summary>
/// 한 파일이 메시·스켈레톤·애니메이션 클립을 함께 낼 때 각 서브에셋 자신의 타입으로
/// 참조를 검증하는지 확인한다. CollectAssetChoices와 AssetReferenceMatchesType은
/// 같은 타입 판정을 사용해야 목록 선택과 인스펙터 끌어 놓기 검증이 일치한다.
/// 각 서브에셋은 자기 타입에만 맞고 파일의 대표 타입이나 다른 서브에셋 타입에는 맞지 않아야 한다.
/// </summary>
[[nodiscard]] bool RunAssetSubAssetTypeTests();
