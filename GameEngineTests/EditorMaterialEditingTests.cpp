#include "EditorMaterialEditingTests.h"

#include <filesystem>
#include <optional>
#include <string>

#include "../GameEditor/Source/Rules/EditorMaterialEditing.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetReference.h"
#include "Assets/MaterialData.h"
#include "Core/Json.h"
#include "Core/TextFile.h"
#include "Math/Color.h"
#include "Platform/DirectoryContentSource.h"
#include "TestSupport.h"

using GameEngine::Assets::AssetDatabase;
using GameEngine::Assets::AssetReference;
using GameEngine::Assets::Material;
using GameEngine::Assets::MaterialData;
using TestSupport::Expect;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

bool RunEditorMaterialEditingTests()
{
    TemporaryDirectory projectDirectory("material-editing");
    const std::filesystem::path root = projectDirectory.GetPath();
    const bool wrote =
        WriteFile(root / "MaterialEditingTest.gameproject", "{}") &&
        WriteFile(root / "Sprites" / "Circle.png", "png-data") &&
        // 손으로 지어낸 항목("note")이 하나 섞여 있다 — 왕복이 이것을 보존하는지가 이 시험의
        // 절반이다.
        WriteFile(root / "Materials" / "WithExtra.material",
            R"({"texture": "", "tint": [1.0, 1.0, 1.0, 1.0], "note": "hand-written"})") &&
        WriteFile(root / "Materials" / "Broken.material", "{ this is not json");
    if (!Expect(wrote, "the material editing test project should be written"))
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

    // ---- 있는 파일: texture·tint만 바뀌고 나머지는 그대로다 ----
    {
        const auto* const material = database.FindAsset<Material>("Materials/WithExtra.material");
        if (!Expect(material != nullptr, "WithExtra.material should register as a Material"))
        {
            return false;
        }
        const MaterialData next{
            AssetReference(std::filesystem::path("Sprites/Circle.png")),
            GameEngine::Math::Color{ 0.25f, 0.5f, 0.75f, 0.875f } };
        passed &= Expect(
            GameEditor::WriteMaterial(*material, next),
            "writing over an existing material file should succeed");

        const std::optional<std::string> text =
            GameEngine::Core::ReadTextFile(root / "Materials" / "WithExtra.material");
        if (!Expect(text.has_value(), "the material file should still be readable after the write"))
        {
            return false;
        }
        const GameEngine::Core::Json json = GameEngine::Core::Json::Parse(*text);
        passed &= Expect(
            json.Value("texture", std::string{}) == "Sprites/Circle.png",
            "the file's texture should be the new reference");
        const GameEngine::Core::Json* const tintJson = json.Find("tint");
        passed &= Expect(
            tintJson && tintJson->IsArray() && tintJson->Size() == 4 &&
                tintJson->At(0).Get<float>() == 0.25f && tintJson->At(1).Get<float>() == 0.5f &&
                tintJson->At(2).Get<float>() == 0.75f && tintJson->At(3).Get<float>() == 0.875f,
            "the file's tint should be the new four channels, in order");
        passed &= Expect(
            json.Value("note", std::string{}) == "hand-written",
            "writing texture and tint should not disturb a field this function does not know");

        // 다시 읽으면 데이터베이스도 같은 값을 낸다 — 파일이 맞는 모양이라는 것과, 그 모양을
        // 임포터가 우리가 방금 쓴 그대로 되읽는다는 것은 다른 주장이다.
        static_cast<void>(database.Refresh(content));
        const std::shared_ptr<const MaterialData> reloaded = database.LoadMaterial(
            AssetReference(std::filesystem::path("Materials/WithExtra.material")));
        passed &= Expect(
            reloaded && reloaded->tint == next.tint && reloaded->texture == next.texture,
            "reloading through the real importer should agree with what was written");
    }

    // ---- 깨진 파일: 되쓰지 않고 거절한다 ----
    {
        const auto* const material = database.FindAsset<Material>("Materials/Broken.material");
        if (!Expect(material != nullptr, "Broken.material should still register as a Material"))
        {
            return false;
        }
        const std::optional<std::string> before =
            GameEngine::Core::ReadTextFile(root / "Materials" / "Broken.material");
        passed &= Expect(
            !GameEditor::WriteMaterial(*material, MaterialData{}),
            "writing over a file that is not valid JSON should be refused");
        const std::optional<std::string> after =
            GameEngine::Core::ReadTextFile(root / "Materials" / "Broken.material");
        passed &= Expect(
            before == after, "a rejected write should leave the broken file untouched");
    }

    return passed;
}

static const TestSupport::Registration gEditorMaterialEditingTests{
    "EditorDocument", "editor material editing tests should pass", RunEditorMaterialEditingTests };
