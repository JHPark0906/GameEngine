#pragma once

/// <summary>
/// WriteSpriteSheet(EditorSpriteSheetEditing.h)이 사이드카를 왕복하는지다: 시트만 바꾸고
/// guid·pixelsPerUnit·border 같은 다른 항목은 그대로 두는지, 사이드카가 아직 없는 에셋에는
/// 최소한의 파일을 새로 놓는지, 격자가 담을 수 없는 시트는 아예 쓰지 않는지.
/// </summary>
[[nodiscard]] bool RunEditorSpriteSheetEditingTests();
