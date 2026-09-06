#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../GameEditor/Source/Document/EditorAssetMaintenance.h"
#include "../GameEngine/App/ProjectFile.h"
#include "../GameEngine/Platform/DirectoryContentSource.h"
#include "../GameEngine/Platform/IAudioOutput.h"
#include "../GameEngine/Platform/ITextMeasure.h"
#include "../GameEngine/Runtime/Game.h"
#include "../GameEngine/Serialization/ComponentSchema.h"
#include "../GameEngine/Serialization/RuntimeComponentFactories.h"
#include "../GameEngine/Serialization/SceneSerializer.h"

#include "AssetMaintenanceIdentityTests.h"
#include "TestSupport.h"

using GameEditor::EditorAssetMaintenance;
using TestSupport::Expect;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{

/// <summary>이관할 것이 하나 있는 최소 프로젝트다. 스프라이트를 경로로 가리킨다.</summary>
[[nodiscard]] std::filesystem::path WriteMigrationProject(
    const std::filesystem::path& root, const std::string& projectName,
    const std::string& spriteGuid)
{
    const std::filesystem::path projectFile = root / (projectName + ".gameproject");
    const std::string scene =
        R"({"sceneName": "First", "gameObjects": [)"
        R"( {"id": 1, "name": "Sprite", "isActive": true, "components": [)"
        R"( {"type": "Transform"},)"
        R"( {"type": "SpriteRenderer", "sprite": "Textures/Albedo.png"} ]} ]})";
    const bool wrote =
        WriteFile(projectFile,
            R"({"projectName": ")" + projectName + R"(",)"
            R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
            R"( "initialSceneId": 0, "scenes": [ { "id": 0, "path": "Scenes/First.scene" } ]})") &&
        WriteFile(root / "Scenes" / "First.scene", scene) &&
        WriteFile(root / "Textures" / "Albedo.png", "albedo-bytes") &&
        WriteFile(root / "Textures" / "Albedo.png.meta",
            R"({"format":"gameengine-meta/1","guid":")" + spriteGuid + R"("})");
    return wrote ? projectFile : std::filesystem::path{};
}

/// <summary>
/// 프로젝트 하나를 이루는 것들이다. 편집기가 프로젝트를 열 때 세우는 것과 같은 것을, 문서
/// 모델 없이 세운다 — 이 시험이 재는 것은 문서가 아니라 손잡이와 조사 결과의 수명이다.
/// </summary>
struct OpenedProject
{
    std::optional<GameEngine::App::ProjectFileData> file;
    std::unique_ptr<GameEngine::Platform::DirectoryContentSource> content;
    std::unique_ptr<GameEngine::Runtime::Game> game;
    std::vector<GameEngine::Serialization::ComponentSchema> schemas;

    [[nodiscard]] bool IsValid() const { return file && content && game; }

    [[nodiscard]] EditorAssetMaintenance::ProjectHandles Handles() const
    {
        return { file ? &*file : nullptr, game.get(), content.get(), &schemas };
    }
};

[[nodiscard]] OpenedProject OpenProject(const std::filesystem::path& projectFile)
{
    OpenedProject opened;
    opened.file = GameEngine::App::ProjectFile::Load(projectFile);
    if (!opened.file)
    {
        return opened;
    }
    const std::filesystem::path root = projectFile.parent_path();
    opened.content =
        std::make_unique<GameEngine::Platform::DirectoryContentSource>(root);
    auto game = std::make_unique<GameEngine::Runtime::Game>(nullptr, nullptr);
    game->SetSceneLoader(GameEngine::Serialization::SceneSerializer::MakeSceneLoader());
    std::unordered_map<unsigned int, std::filesystem::path> scenePaths;
    for (const auto& [sceneId, relativeScenePath] : opened.file->settings.scenePaths)
    {
        scenePaths.emplace(sceneId, relativeScenePath);
    }
    if (!game->Initialize(*opened.content, scenePaths))
    {
        return {};
    }
    opened.game = std::move(game);
    return opened;
}

}

bool RunAssetMaintenanceIdentityTests()
{
    std::cout << "running asset maintenance identity tests\n";

    // 장면을 읽으려면 컴포넌트 공장이 등록되어 있어야 한다. 등록은 한 번이면 되고 다른 시험이
    // 이미 했을 수 있으므로 결과를 보지 않는다.
    static_cast<void>(GameEngine::Serialization::RegisterRuntimeComponentFactories());

    TemporaryDirectory temporaryDirectory("asset-maintenance-identity");
    const std::filesystem::path root = temporaryDirectory.GetPath();

    const std::filesystem::path fileA = WriteMigrationProject(
        root / "A", "ProjectA", "5a4b3c2d1e0f9a8b7c6d5e4f3a2b1c0d");
    const std::filesystem::path fileB = WriteMigrationProject(
        root / "B", "ProjectB", "0d1c2b3a4f5e6d7c8b9a0f1e2d3c4b5a");
    if (!Expect(!fileA.empty() && !fileB.empty(), "both test projects should be written"))
    {
        return false;
    }

    const OpenedProject a = OpenProject(fileA);
    const OpenedProject b = OpenProject(fileB);
    if (!Expect(a.IsValid() && b.IsValid(), "both test projects should open"))
    {
        return false;
    }

    EditorAssetMaintenance maintenance;
    maintenance.SurveySceneMigration(a.Handles());
    const GameEditor::SceneMigrationPlan* const plan = maintenance.GetSceneMigrationPlan();
    if (!Expect(
            plan != nullptr && !plan->IsBlocked() && plan->convertible == 1,
            "project A should have one reference to migrate"))
    {
        return false;
    }

    const std::string sceneBBefore = TestSupport::ReadFile(root / "B" / "Scenes" / "First.scene");

    // 다른 프로젝트의 손잡이로 적용하려 하면 거절한다. 거절은 조용하지 않다 — 로그가 어느
    // 프로젝트를 훑었고 지금 어느 것이 열려 있는지 말한다.
    const EditorAssetMaintenance::SceneMigrationOutcome refused =
        maintenance.ApplySceneMigration(b.Handles());
    bool passed = Expect(
        !refused.applied,
        "a plan surveyed for one project should not be applied to another");
    passed = Expect(
        !refused.shouldReloadOpenScene,
        "a refused migration should not ask for the open scene to be reloaded") && passed;
    passed = Expect(
        TestSupport::ReadFile(root / "B" / "Scenes" / "First.scene") == sceneBBefore,
        "a refused migration should leave the other project's scene untouched") && passed;
    passed = Expect(
        maintenance.GetSceneMigrationPlan() != nullptr,
        "a refused migration should keep the plan, since it is still true of its own project")
        && passed;

    // 훑은 그 프로젝트로는 적용된다. 거절이 계획 자체를 못 쓰게 만든 것이 아니라는 뜻이다.
    const EditorAssetMaintenance::SceneMigrationOutcome applied =
        maintenance.ApplySceneMigration(a.Handles());
    passed = Expect(
        applied.applied, "the plan should apply to the project it was surveyed for") && passed;
    passed = Expect(
        applied.shouldReloadOpenScene,
        "applying should tell the caller to reload the open scene") && passed;
    passed = Expect(
        maintenance.AreSceneReferencesMigrated(),
        "applying should mark this project's references migrated") && passed;

    return passed;
}

static const TestSupport::Registration gAssetMaintenanceIdentityTests{
    "EditorDocument", "asset maintenance identity tests should pass",
    RunAssetMaintenanceIdentityTests };
