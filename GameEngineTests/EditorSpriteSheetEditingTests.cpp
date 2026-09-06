#include "EditorSpriteSheetEditingTests.h"

#include <filesystem>
#include <optional>
#include <string>

#include "../GameEditor/Source/Rules/EditorSpriteSheetEditing.h"
#include "Assets/AssetDatabase.h"
#include "Core/Json.h"
#include "Platform/TextFile.h"
#include "Platform/DirectoryContentSource.h"
#include "TestSupport.h"

using GameEngine::Assets::AssetDatabase;
using GameEngine::Assets::Sprite;
using TestSupport::Expect;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

bool RunEditorSpriteSheetEditingTests()
{
    TemporaryDirectory projectDirectory("sprite-sheet-editing");
    const std::filesystem::path root = projectDirectory.GetPath();
    const bool wrote =
        WriteFile(root / "SpriteSheetTest.gameproject", "{}") &&
        // 있는 사이드카: guid·pixelsPerUnit·border를 이미 담고 있다.
        WriteFile(root / "Sprites" / "WithSidecar.png", "png-data") &&
        WriteFile(root / "Sprites" / "WithSidecar.png.meta",
            R"({"format": "gameengine-meta/1", "guid": "00000000000000000000000000000001",)"
            R"( "pixelsPerUnit": 64, "border": [1, 2, 3, 4],)"
            R"( "sheet": {"columns": 4, "rows": 4, "frameCount": 16, "frameRate": 12}})") &&
        // 사이드카가 아직 없는 스프라이트.
        WriteFile(root / "Sprites" / "NoSidecar.png", "png-data");
    if (!Expect(wrote, "the sprite sheet editing test project should be written"))
    {
        return false;
    }

    const GameEngine::Platform::DirectoryContentSource content(root);
    AssetDatabase database;
    if (!Expect(database.Refresh(content), "the test project should scan"))
    {
        return false;
    }

    bool passed = true;

    // ---- 있는 사이드카: 시트만 바뀌고 나머지는 그대로다 ----
    {
        const auto* const sprite = database.FindAsset<Sprite>("Sprites/WithSidecar.png");
        if (!Expect(sprite != nullptr, "WithSidecar.png should register as a Sprite"))
        {
            return false;
        }
        const Sprite::Sheet newSheet{ .columns = 8, .rows = 8, .frameCount = 32, .frameRate = 10.0f };
        passed &= Expect(
            GameEditor::WriteSpriteSheet(database, *sprite, newSheet),
            "writing a valid sheet over an existing sidecar should succeed");

        const std::optional<std::string> text =
            GameEngine::Platform::ReadTextFile(root / "Sprites" / "WithSidecar.png.meta");
        if (!Expect(text.has_value(), "the sidecar should still be readable after the write"))
        {
            return false;
        }
        const GameEngine::Core::Json json = GameEngine::Core::Json::Parse(*text);
        const GameEngine::Core::Json* const sheetJson = json.Find("sheet");
        passed &= Expect(
            sheetJson && sheetJson->Value("columns", 0) == 8 && sheetJson->Value("rows", 0) == 8 &&
                sheetJson->Value("frameCount", 0) == 32 &&
                sheetJson->Value("frameRate", 0.0f) == 10.0f,
            "the sidecar's sheet should be the new values");
        passed &= Expect(
            json.Value("guid", std::string{}) == "00000000000000000000000000000001",
            "writing the sheet should not touch the existing guid");
        passed &= Expect(
            json.Value("pixelsPerUnit", 0.0f) == 64.0f,
            "writing the sheet should not touch pixelsPerUnit");
        const GameEngine::Core::Json* const borderJson = json.Find("border");
        passed &= Expect(
            borderJson && borderJson->IsArray() && borderJson->Size() == 4,
            "writing the sheet should not touch border");
    }

    // ---- 사이드카가 없던 에셋: 최소한의 새 사이드카가 놓인다 ----
    {
        const auto* const sprite = database.FindAsset<Sprite>("Sprites/NoSidecar.png");
        if (!Expect(sprite != nullptr, "NoSidecar.png should register as a Sprite"))
        {
            return false;
        }
        passed &= Expect(
            database.GetSidecarPath(*sprite).empty(),
            "NoSidecar.png should not have a sidecar yet, or the test fixture is wrong");

        const Sprite::Sheet newSheet{ .columns = 2, .rows = 1, .frameCount = 2, .frameRate = 6.0f };
        passed &= Expect(
            GameEditor::WriteSpriteSheet(database, *sprite, newSheet),
            "writing a sheet with no existing sidecar should still succeed");

        const std::optional<std::string> text =
            GameEngine::Platform::ReadTextFile(root / "Sprites" / "NoSidecar.png.meta");
        if (!Expect(text.has_value(), "a new sidecar should have been placed next to the source"))
        {
            return false;
        }
        const GameEngine::Core::Json json = GameEngine::Core::Json::Parse(*text);
        passed &= Expect(
            json.Value("format", std::string{}) == "gameengine-meta/1",
            "a freshly written sidecar should still carry the format tag");
        passed &= Expect(
            !json.Find("guid"), "writing the sheet must never invent a guid -- issuing one is a "
            "separate, single-call-site operation");
        const GameEngine::Core::Json* const sheetJson = json.Find("sheet");
        passed &= Expect(
            sheetJson && sheetJson->Value("columns", 0) == 2 && sheetJson->Value("frameCount", 0) == 2,
            "the freshly written sidecar should carry the new sheet");
    }

    // ---- 격자가 담을 수 없는 시트는 거절되고, 사이드카는 손대지 않는다 ----
    {
        const auto* const sprite = database.FindAsset<Sprite>("Sprites/WithSidecar.png");
        const std::optional<std::string> before =
            GameEngine::Platform::ReadTextFile(root / "Sprites" / "WithSidecar.png.meta");
        const Sprite::Sheet invalidSheet{ .columns = 0, .rows = 4, .frameCount = 0, .frameRate = 10.0f };
        passed &= Expect(
            sprite && !GameEditor::WriteSpriteSheet(database, *sprite, invalidSheet),
            "a sheet with zero columns should be rejected");
        const std::optional<std::string> after =
            GameEngine::Platform::ReadTextFile(root / "Sprites" / "WithSidecar.png.meta");
        passed &= Expect(
            before == after, "a rejected write should leave the sidecar file untouched");
    }

    return passed;
}

static const TestSupport::Registration gEditorSpriteSheetEditingTests{
    "EditorDocument", "editor sprite sheet editing tests should pass",
    RunEditorSpriteSheetEditingTests };
