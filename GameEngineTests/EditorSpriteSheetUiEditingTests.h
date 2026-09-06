#pragma once

/// <summary>
/// EditorSpriteSheetEditingTests.cpp가 WriteSpriteSheet를 곧장 부르는 것과 달리, 이것은
/// EditorInspectorPanel::Draw가 실제로 그리는 Columns 칸에 가짜 입력으로 타이핑해, 그
/// 배선(칸 → apply 콜백 → WriteSpriteSheet)이 실제로 이어지는지를 잰다. 앞의 시험이
/// WriteSpriteSheet 자신이 옳다는 것을, 이것은 그 앞이 실제로 그것을 부른다는 것을 증명한다 —
/// 둘 다 있어야 "사람이 칸에 타이핑하면 파일이 바뀐다"는 실제로 지켜지는 계약이 된다.
/// </summary>
[[nodiscard]] bool RunEditorSpriteSheetUiEditingTests();
