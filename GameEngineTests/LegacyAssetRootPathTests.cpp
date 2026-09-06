#include "LegacyAssetRootPathTests.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "../GameEngine/App/ProjectFile.h"
#include "../GameEngine/Diagnostics/Debug.h"

#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

bool RunLegacyAssetRootPathTests()
{
    std::cout << "running legacy asset root path tests\n";

    TemporaryDirectory temporaryDirectory("legacy-asset-root");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::filesystem::path projectFile = root / "Legacy.gameproject";
    // 값은 "."이 아닌 무엇이든 상관없다 — 무시된다는 것을 재는 시험이라, 오히려 뜻이 다른
    // 값이어야 「우연히 답이 맞았다」가 아니게 된다.
    const bool wrote =
        WriteFile(projectFile,
            R"({"projectName": "Legacy", "assetRootPath": "SomewhereElse",)"
            R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
            R"( "initialSceneId": 0, "scenes": [ { "id": 0, "path": "Scenes/Main.scene" } ]})") &&
        WriteFile(root / "Scenes" / "Main.scene", R"({"sceneName": "Main", "gameObjects": []})");
    if (!Expect(wrote, "the legacy project file should be written"))
    {
        return false;
    }

    std::vector<std::string> warnings;
    const GameEngine::Diagnostics::Debug::LogListenerId listener =
        GameEngine::Diagnostics::Debug::AddLogListener(
            [&warnings](const GameEngine::Diagnostics::LogEntry& entry)
            {
                if (entry.level == GameEngine::Diagnostics::LogLevel::Warning)
                {
                    warnings.push_back(entry.message);
                }
            });
    const std::optional<GameEngine::App::ProjectFileData> project =
        GameEngine::App::ProjectFile::Load(projectFile);
    GameEngine::Diagnostics::Debug::RemoveLogListener(listener);

    bool passed = Expect(
        project.has_value(), "a project that still carries the field should open, not be refused");
    passed = Expect(
        project && project->GetAssetRootPath() == root,
        "the field's value should be ignored -- the asset root is the project file's own directory")
        && passed;

    const auto namesTheField = [](const std::string& message)
    {
        return message.find("assetRootPath") != std::string::npos;
    };
    const std::size_t mentions =
        static_cast<std::size_t>(std::count_if(warnings.begin(), warnings.end(), namesTheField));
    passed = Expect(mentions == 1, "opening the project should warn about the field exactly once")
        && passed;

    return passed;
}

static const TestSupport::Registration gLegacyAssetRootPathTests{
    "AssetDatabase", "legacy asset root path tests should pass", RunLegacyAssetRootPathTests };
