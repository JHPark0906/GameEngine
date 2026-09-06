#pragma once

/// <summary>
/// Play 진입은 한 번만 적용되고 End는 Begin에 전달한 장면 텍스트를 그대로 반환해야 한다.
/// 문서를 교체하면 재생 스냅샷을 비우되 저장되지 않은 편집 상태는 재생만으로 지우지 않는다.
/// </summary>
[[nodiscard]] bool RunEditorPlaySessionTests();
