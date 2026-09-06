#include "ProjectBuildIsolationTests.h"

#include <filesystem>
#include <string>

#include "Build/ProjectBuilder.h"
#include "Platform/PackedContentSource.h"
#include "Platform/PlatformServices.h"
#include "TestSupport.h"

bool RunProjectBuildIsolationTests()
{
    namespace fs = std::filesystem;
    using TestSupport::Expect;
    using TestSupport::ReadFile;
    using TestSupport::WriteFile;
    TestSupport::TemporaryDirectory temporary("build-isolation-regression");
    const fs::path root = temporary.GetPath();
    // A scratch directory name must not collide with a valid source project directory.
    const fs::path source = root / "Game.staging";
    const fs::path content = source / "Content";
    const fs::path compiled = root / "Compiled";
    const fs::path output = root / "Game";
    const fs::path unrelatedBackup = root / "Game.backup";
    const bool prepared =
        WriteFile(source / "CMakeLists.txt", "source-owned build script") &&
        WriteFile(content / "BuildIsolation.gameproject",
            R"({"projectName":"BuildIsolation","initialSceneId":0,"window":{"width":1280,"height":720},)"
            R"("scenes":[{"id":0,"path":"Scenes/Main.scene"}]})") &&
        WriteFile(content / "Scenes/Main.scene", R"({"sceneName":"Main","gameObjects":[]})") &&
        WriteFile(compiled / "BuildIsolation.exe", "test player") &&
        WriteFile(compiled / "Dependency.DLL", "native dependency") &&
        WriteFile(unrelatedBackup / "Keep.txt", "unrelated backup") &&
        WriteFile(output / "OldOutput.txt", "old package");
    if (!Expect(prepared, "the build isolation fixture should be written")) return false;

    GameEngine::Build::ProjectBuildRequest request{
        content, compiled / "BuildIsolation.exe",
        GameEngine::Platform::PlatformServices::GetExecutableDirectory(), output, true, true };
    const auto built = GameEngine::Build::ProjectBuilder::Build(request);
    bool passed = Expect(built.has_value(), "packing should succeed beside a .staging source tree");
    passed = Expect(ReadFile(source / "CMakeLists.txt") == "source-owned build script",
        "packaging must preserve a source directory that ends in .staging") && passed;
    passed = Expect(ReadFile(unrelatedBackup / "Keep.txt") == "unrelated backup",
        "packaging must not delete an unrelated .backup directory") && passed;
    passed = Expect(ReadFile(output / "Dependency.DLL") == "native dependency",
        "native DLL dependencies must remain readable by the OS loader") && passed;
    {
        const GameEngine::Platform::PackedContentSource pack(output / "BuildIsolation.exe");
        passed = Expect(pack.IsValid() && pack.Exists("Scenes/Main.scene"),
            "the executable should hold a readable project content pack") && passed;
        passed = Expect(!pack.Exists("Dependency.DLL"),
            "native dependencies must not be hidden inside the content pack") && passed;
    }
    passed = Expect(!fs::exists(output / "Scenes/Main.scene"),
        "only the packed copy of scene content should remain") && passed;

    GameEngine::Build::ProjectBuildRequest nestedRequest = request;
    nestedRequest.outputPath = root / "Builds/nested/Game";
    passed = Expect(!fs::exists(nestedRequest.outputPath.parent_path()),
        "the nested output parent must not exist before packaging") && passed;
    const auto nestedBuild = GameEngine::Build::ProjectBuilder::Build(nestedRequest);
    passed = Expect(nestedBuild.has_value(),
        "packaging should create missing output ancestors before reserving its workspace") && passed;
    {
        const GameEngine::Platform::PackedContentSource pack(
            nestedRequest.outputPath / "BuildIsolation.exe");
        passed = Expect(pack.IsValid() && pack.Exists("Scenes/Main.scene") &&
            ReadFile(nestedRequest.outputPath / "Dependency.DLL") == "native dependency",
            "packaging under newly created parents should publish content and native dependencies") && passed;
    }
    if (nestedBuild)
    {
        for (const fs::directory_entry& entry : fs::directory_iterator(nestedRequest.outputPath.parent_path()))
        {
            passed = Expect(!entry.path().filename().string().starts_with("Game.build-"),
                "a nested build should clean up only its owned workspace") && passed;
        }
    }

    // A later failure must leave the successful package and every unrelated sibling intact.
    passed = Expect(WriteFile(output / "KeepSuccess.txt", "last successful package"),
        "the previous successful package marker should be written") && passed;
    passed = Expect(WriteFile(content / "BuildIsolation.gameproject",
        R"({"projectName":"BuildIsolation","initialSceneId":0,"window":{"width":1280,"height":720},)"
        R"("scenes":[{"id":0,"path":"Scenes/Missing.scene"}]})"),
        "the failed build input should be written") && passed;
    passed = Expect(!GameEngine::Build::ProjectBuilder::Build(request),
        "a package with a missing scene should fail") && passed;
    passed = Expect(ReadFile(output / "KeepSuccess.txt") == "last successful package",
        "failed packaging should preserve the last successful output") && passed;
    passed = Expect(ReadFile(unrelatedBackup / "Keep.txt") == "unrelated backup",
        "failure cleanup must preserve the unrelated backup") && passed;
    for (const fs::directory_entry& entry : fs::directory_iterator(root))
    {
        passed = Expect(!entry.path().filename().string().starts_with("Game.build-"),
            "completed builds should remove their own temporary workspaces") && passed;
    }
    return passed;
}

static const TestSupport::Registration gProjectBuildIsolationTests{
    "ProjectBuilder", "isolated packaging preserves sources and native dependencies",
    RunProjectBuildIsolationTests };
