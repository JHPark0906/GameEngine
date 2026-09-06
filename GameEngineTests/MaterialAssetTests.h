#pragma once

/// <summary>
/// Material 에셋 종류가 실제로 배선됐는지 창 없이 고정한다: 텍스처 참조와 tint를 담은
/// <c>.material</c> 파일이 저장한 그대로 다시 읽히는지, 그리고 인스펙터의 선택 목록이 그것을
/// 다른 종류와 섞지 않고 골라내는지.
/// </summary>
[[nodiscard]] bool RunMaterialAssetTests();
