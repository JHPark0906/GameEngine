#pragma once

/// <summary>
/// WriteMaterial이 <c>.material</c> 파일을 있는 그대로 되쓰는지, 모르는 항목을 보존하는지,
/// 깨진 JSON은 건드리지 않고 거절하는지를 창 없이 고정한다.
/// </summary>
[[nodiscard]] bool RunEditorMaterialEditingTests();
