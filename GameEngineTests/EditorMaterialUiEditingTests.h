#pragma once

/// <summary>
/// 실제 EditorInspectorPanel::Draw 호출 경로로, Material 에셋을 고르면 Texture(에셋 참조
/// 드롭다운)·Tint(색 편집) 칸이 실제로 그려지고 눌러 고치면 그 <c>.material</c> 파일에
/// 반영되는지를 창 없이 고정한다.
/// </summary>
[[nodiscard]] bool RunEditorMaterialUiEditingTests();
