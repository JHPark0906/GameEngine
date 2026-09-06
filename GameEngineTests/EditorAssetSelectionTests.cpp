#include "EditorAssetSelectionTests.h"

#include <filesystem>

#include "../GameEditor/Source/Document/EditorContext.h"
#include "TestSupport.h"

using GameEditor::EditorContext;
using TestSupport::Expect;

bool RunEditorAssetSelectionTests()
{
    EditorContext context;
    bool passed = true;

    // ---- 에셋을 고르면 오브젝트 선택이 풀린다 ----
    context.SelectObject(1234);
    passed &= Expect(
        context.GetSelectedInstanceId() == 1234, "selecting an object should record its id");
    context.SelectAsset(std::filesystem::path("Sprites") / "tile.png");
    passed &= Expect(
        context.GetSelectedInstanceId() == 0,
        "selecting an asset should clear the object selection");
    passed &= Expect(
        context.GetSelectedAssetPath() == std::filesystem::path("Sprites") / "tile.png",
        "the selected asset's path should be recorded");

    // ---- 오브젝트를 고르면 에셋 선택이 풀린다 ----
    context.SelectObject(5678);
    passed &= Expect(
        !context.GetSelectedAssetPath().has_value(),
        "selecting an object should clear the asset selection");
    passed &= Expect(
        context.GetSelectedInstanceId() == 5678, "the selected object's id should be recorded");

    // ---- 0으로 고르면(선택 해제) 에셋 선택도 풀린다 ----
    context.SelectAsset(std::filesystem::path("Sprites") / "other.png");
    context.SelectObject(0);
    passed &= Expect(
        !context.GetSelectedAssetPath().has_value(),
        "deselecting the object (id 0) should also clear any asset selection");

    // ---- 명시적으로 지울 수도 있다 ----
    context.SelectAsset(std::filesystem::path("Sprites") / "tile.png");
    context.ClearAssetSelection();
    passed &= Expect(
        !context.GetSelectedAssetPath().has_value(), "ClearAssetSelection should clear it");

    return passed;
}

static const TestSupport::Registration gEditorAssetSelectionTests{
    "EditorDocument", "editor asset selection tests should pass", RunEditorAssetSelectionTests };
