#include "MaterialAssetTests.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Assets/AssetDatabase.h"
#include "Platform/DirectoryContentSource.h"
#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

/// <summary>
/// Material 페이로드의 스캔과 선택지가 다른 에셋 타입과 구분되는지 확인한다.
/// 편집기에서 Material 파일을 만드는 동작은 이 검사의 범위에 포함하지 않는다.
/// </summary>
namespace
{
    using namespace GameEngine;

    constexpr std::string_view TextureGuid = "a1b2c3d4e5f60718293a4b5c6d7e8f90";
    constexpr std::string_view MaterialGuid = "0f1e2d3c4b5a69788796a5b4c3d2e1f0";

    /// <summary>
    /// 최소 프로젝트 하나에 텍스처 하나와 그것을 가리키는 머티리얼 하나를 놓는다. 텍스처
    /// 바이트는 진짜 PNG가 아니다 — 이 시험이 재는 것은 머티리얼의 참조가 그 경로를 그대로
    /// 담아 오는지이지, 픽셀이 디코딩되는지가 아니다.
    /// </summary>
    [[nodiscard]] std::filesystem::path WriteProjectWithMaterial(
        const std::filesystem::path& root)
    {
        const std::string textureSidecar =
            R"({"format":"gameengine-meta/1","guid":")" + std::string(TextureGuid) + R"("})";
        const std::string materialSidecar =
            R"({"format":"gameengine-meta/1","guid":")" + std::string(MaterialGuid) + R"("})";
        // 순서 없는 객체로 파싱되므로 필드 순서는 뜻이 없다. tint는 넷 다 서로 달라야
        // (r,g,b,a) 중 하나가 뒤바뀐 결함도 이 시험이 잡는다.
        const std::string materialJson =
            R"({"texture": "circle-16.png", "tint": [0.25, 0.5, 0.75, 0.875]})";

        const std::filesystem::path projectFile = root / "MaterialAssetTest.gameproject";
        const bool wrote =
            WriteFile(projectFile,
                R"({"projectName": "MaterialAssetTest", "assetRootPath": ".",)"
                R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
                R"( "initialSceneId": 0, "scenes": [] })") &&
            WriteFile(root / "circle-16.png", "not-real-png-bytes") &&
            WriteFile(root / "circle-16.png.meta", textureSidecar) &&
            WriteFile(root / "Basic.material", materialJson) &&
            WriteFile(root / "Basic.material.meta", materialSidecar);
        return wrote ? projectFile : std::filesystem::path{};
    }
}

bool RunMaterialAssetTests()
{
    std::cout << "running material asset tests\n";

    TemporaryDirectory temporaryDirectory("material-asset");
    const std::filesystem::path projectFile =
        WriteProjectWithMaterial(temporaryDirectory.GetPath());
    if (!Expect(!projectFile.empty(), "the material fixture should be written"))
    {
        return false;
    }

    const Platform::DirectoryContentSource content(temporaryDirectory.GetPath());
    Assets::AssetDatabase database;
    if (!Expect(database.Refresh(content), "the fixture project should refresh cleanly"))
    {
        return false;
    }

    // 🔴 CollectAssetChoices가 Material만 고르는지 — Sprite와 섞이지 않는지가 이 시험의
    // 절반이다. 스프라이트가 하나 있는 프로젝트에서 재므로, 섞였다면 여기서 둘이 보인다.
    const std::vector<Assets::AssetChoice> materialChoices =
        Assets::CollectAssetChoices(database, Assets::AssetType::Material);
    const std::vector<Assets::AssetChoice> spriteChoices =
        Assets::CollectAssetChoices(database, Assets::AssetType::Sprite);

    bool passed = Expect(
        materialChoices.size() == 1, "the choice list should hold exactly the one material");
    passed = Expect(
        spriteChoices.size() == 1, "and the sprite should still be findable under its own type")
        && passed;
    passed = Expect(
        materialChoices.empty() ||
            materialChoices.front().label.find("Basic.material") != std::string::npos,
        "the material's choice should be labelled after its file") && passed;

    if (materialChoices.empty())
    {
        std::cout << "  no material choice to load; stopping here\n";
        return passed;
    }

    // 🔴 왕복: 저장한 그대로 읽히는가. tint의 네 채널이 다 다르므로, 성분이 뒤바뀌는 결함도
    // 여기서 드러난다.
    const std::shared_ptr<const Assets::MaterialData> material =
        database.LoadMaterial(materialChoices.front().reference);
    if (!Expect(material != nullptr, "the material should load a payload"))
    {
        return false;
    }

    const Math::Color expectedTint{ 0.25f, 0.5f, 0.75f, 0.875f };
    passed = Expect(
        material->tint == expectedTint,
        "the loaded tint should match the four channels the file declared") && passed;
    passed = Expect(
        material->texture.IsValid() && !material->texture.IsGuidReference() &&
            material->texture.GetPath() == std::filesystem::path("circle-16.png"),
        "the loaded texture reference should be the path the file named") && passed;

    // 참조가 실제로 그 스프라이트에 닿는지도 잰다 — 경로 문자열이 같아 보여도 데이터베이스가
    // 다른 파일로 해석하면 그리는 쪽은 엉뚱한 텍스처를 받는다.
    passed = Expect(
        Assets::AssetReferenceMatchesType(database, material->texture, Assets::AssetType::Sprite),
        "and that reference should resolve to a real sprite in this project") && passed;

    return passed;
}

static const TestSupport::Registration gMaterialAssetTests{
    "AssetDatabase", "material asset tests should pass", RunMaterialAssetTests };
