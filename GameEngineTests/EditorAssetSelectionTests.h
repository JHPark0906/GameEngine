#pragma once

/// <summary>
/// 오브젝트 선택과 에셋 선택은 한 번에 하나뿐이다. 콘텐츠 브라우저에서 에셋을 고르면
/// 계층의 GameObject 선택이 풀리고, 오브젝트를 고르면 에셋 선택이 풀리는지 확인한다.
/// </summary>
[[nodiscard]] bool RunEditorAssetSelectionTests();
