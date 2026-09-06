#include "PruneStaleFilesTests.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "Build/CMakeLocation.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>이 파일은 &lt;repo&gt;/GameEngineTests/PruneStaleFilesTests.cpp에 있다.</summary>
    [[nodiscard]] std::filesystem::path RepositoryRoot()
    {
        return std::filesystem::path(__FILE__).parent_path().parent_path();
    }

    [[nodiscard]] std::filesystem::path PruneScript()
    {
        return RepositoryRoot() / "cmake" / "PruneStaleFiles.cmake";
    }

    /// <summary>
    /// 매니페스트 파일 하나를 쓴다 - 상대 경로를 한 줄에 하나씩. 빈 목록도 빈 파일로 정직하게
    /// 쓴다: PruneStaleFiles.cmake가 「지금은 아무것도 스테이징하지 않는다」를 받아들이는지도
    /// 시험 대상이다.
    /// </summary>
    [[nodiscard]] bool WriteManifest(
        const std::filesystem::path& path, const std::vector<std::string>& relativePaths)
    {
        std::string contents;
        for (const std::string& relative : relativePaths)
        {
            contents += relative + "\n";
        }
        return TestSupport::WriteFile(path, contents);
    }

    /// <summary>PruneStaleFiles.cmake 한 번을 돌리고 그 결과를 돌려준다.</summary>
    [[nodiscard]] TestSupport::CommandResult RunPrune(
        const std::filesystem::path& cmake, const std::filesystem::path& outputDir,
        const std::filesystem::path& currentManifest, const std::filesystem::path& previousManifest)
    {
        return TestSupport::RunCommand(
            cmake,
            { "-DOUTPUT_DIR=" + outputDir.generic_string(),
              "-DCURRENT_MANIFEST=" + currentManifest.generic_string(),
              "-DPREVIOUS_MANIFEST=" + previousManifest.generic_string(), "-P",
              PruneScript().generic_string() });
    }
}

/// <summary>
/// 스크립트 하나만 격리해 반복 실행한다: 실제 프로젝트를 configure하지 않으므로 빠르고, 그래서
/// 이 스크립트가 다뤄야 하는 경계 상황 - 첫 빌드(이전 매니페스트가 아직 없음), 지운 파일과
/// 남은 파일이 한 디렉터리에 섞여 있음, 지우고 나서 디렉터리가 완전히 비면 그 디렉터리까지
/// 걷어내되 OUTPUT_DIR 자신과 다른 디렉터리는 손대지 않음, 아예 존재한 적 없는 OUTPUT_DIR -
/// 를 하나씩 정확히 짚을 수 있다.
/// </summary>
bool RunPruneStaleFilesScriptTests()
{
    TestSupport::TemporaryDirectory workspace("prune-stale-files-script");
    const std::filesystem::path root = workspace.GetPath();
    bool passed = true;

    const std::optional<std::filesystem::path> cmake = GameEngine::Build::ResolveCMakePath({}, {});
    if (!Expect(cmake.has_value(), "cmake should be on PATH or beside Visual Studio"))
    {
        return false;
    }

    const std::filesystem::path outputDir = root / "out";
    const std::filesystem::path previousManifest = root / "previous.txt";

    // ⒜ 지워야 할 것도, 기억해 둔 이전 매니페스트도 없는 첫 빌드다: 아무것도 지우지 않고 지금
    // 놓은 것을 다음을 위해 적어 두기만 해야 한다.
    const std::filesystem::path stagedKeep = outputDir / "Sprites" / "Keep.png";
    const std::filesystem::path stagedDropMe = outputDir / "Sprites" / "DropMe.png";
    const std::filesystem::path stagedDropMeMeta = outputDir / "Sprites" / "DropMe.png.meta";
    // 소스 글롭이 결코 잡지 않는, 편집기가 실행 중에 스스로 쓰는 파일들이다 - 스테이징된 것과
    // 같은 디렉터리에 놓아, 디렉터리 하나를 통째로 훑어 판정하면 안 된다는 것을 같이 잰다.
    const std::filesystem::path runtimeSprite = outputDir / "Sprites" / "Keep.png.sprite.json";
    const std::filesystem::path runtimeSettings = outputDir / "GameEditor.settings.json";
    passed &= Expect(TestSupport::WriteFile(stagedKeep, "keep"), "Keep.png should be staged");
    passed &= Expect(TestSupport::WriteFile(stagedDropMe, "drop"), "DropMe.png should be staged");
    passed &= Expect(
        TestSupport::WriteFile(stagedDropMeMeta, "meta"), "DropMe.png.meta should be staged");
    passed &= Expect(
        TestSupport::WriteFile(runtimeSprite, "sprite-json"),
        "the runtime sprite metadata should be written");
    passed &= Expect(
        TestSupport::WriteFile(runtimeSettings, "settings"),
        "the runtime settings file should be written");

    const std::filesystem::path currentFull = root / "current-full.txt";
    passed &= Expect(
        WriteManifest(currentFull, { "Sprites/Keep.png", "Sprites/DropMe.png",
                                      "Sprites/DropMe.png.meta" }),
        "the first manifest should be written");

    const TestSupport::CommandResult firstRun =
        RunPrune(*cmake, outputDir, currentFull, previousManifest);
    passed &= Expect(
        firstRun.exitCode.value_or(-1) == 0, "a first run with no previous manifest should succeed");
    passed &= Expect(
        std::filesystem::exists(stagedKeep) && std::filesystem::exists(stagedDropMe) &&
            std::filesystem::exists(stagedDropMeMeta),
        "a first run should stage nothing away, since there is nothing yet to compare against");
    passed &= Expect(
        std::filesystem::exists(previousManifest), "a first run should still write its memory");
    if (firstRun.exitCode.value_or(-1) != 0)
    {
        std::cerr << firstRun.output << "\n";
    }

    // ⒝ DropMe.png와 그 사이드카가 소스에서 사라졌다: 다음 매니페스트는 Keep.png만 안다. 그
    // 둘만 지워져야 하고, 같은 디렉터리의 런타임 파일과 아직 남아 있는 Keep.png는 매니페스트에
    // 한 번도 실린 적 없거나(런타임 파일) 지금도 실려 있으므로(Keep.png) 손대지 않아야 한다.
    const std::filesystem::path currentAfterDrop = root / "current-after-drop.txt";
    passed &= Expect(
        WriteManifest(currentAfterDrop, { "Sprites/Keep.png" }),
        "the second manifest should be written");

    const TestSupport::CommandResult secondRun =
        RunPrune(*cmake, outputDir, currentAfterDrop, previousManifest);
    passed &= Expect(
        secondRun.exitCode.value_or(-1) == 0, "pruning a dropped file should succeed");
    passed &= Expect(
        !std::filesystem::exists(stagedDropMe) && !std::filesystem::exists(stagedDropMeMeta),
        "the staged copy of a file dropped from source, and its sidecar, should be gone");
    passed &= Expect(
        std::filesystem::exists(stagedKeep), "a file still in the manifest should be untouched");
    passed &= Expect(
        std::filesystem::exists(runtimeSprite) && std::filesystem::exists(runtimeSettings),
        "files this script never staged should never be candidates for removal, no matter what "
        "else shares their directory");
    passed &= Expect(
        std::filesystem::is_directory(outputDir / "Sprites"),
        "the Sprites directory should survive, since a runtime file still lives in it");
    if (secondRun.exitCode.value_or(-1) != 0)
    {
        std::cerr << secondRun.output << "\n";
    }

    // ⒞ Keep.png도 소스에서 사라졌다. 스프라이트 사이드카가 여전히 그 디렉터리에 있으므로
    // 디렉터리 자체는 지워지면 안 된다 - 파일 하나가 없어졌다고 그 자리 전체를 지우면, 파일과
    // 무관하게 같은 자리에 있던 것까지 함께 잃는다.
    const std::filesystem::path currentEmpty = root / "current-empty.txt";
    passed &= Expect(WriteManifest(currentEmpty, {}), "an empty manifest should be written");
    const TestSupport::CommandResult thirdRun =
        RunPrune(*cmake, outputDir, currentEmpty, previousManifest);
    passed &= Expect(thirdRun.exitCode.value_or(-1) == 0, "pruning the last file should succeed");
    passed &= Expect(!std::filesystem::exists(stagedKeep), "the last content file should be gone");
    passed &= Expect(
        std::filesystem::exists(runtimeSprite),
        "a runtime file sharing the now content-empty directory should still be untouched");
    passed &= Expect(
        std::filesystem::is_directory(outputDir / "Sprites"),
        "the directory itself should survive as long as anything at all remains in it");
    if (thirdRun.exitCode.value_or(-1) != 0)
    {
        std::cerr << thirdRun.output << "\n";
    }

    // ⒟ 이번에는 런타임 파일이 없는 디렉터리다: 그 안의 유일한 파일이 스테이징에서 빠지면
    // 디렉터리 자체도 걷어내야 하고, 그 위 디렉터리들도 비었다면 마찬가지로 걷어내야 한다 -
    // 다만 OUTPUT_DIR 자신은 결코 지우지 않는다.
    const std::filesystem::path shader =
        outputDir / "Rendering" / "Direct3D" / "Shaders" / "Mesh.hlsl";
    passed &= Expect(TestSupport::WriteFile(shader, "hlsl"), "the shader fixture should be written");
    const std::filesystem::path shaderManifest = root / "shader-manifest.txt";
    const std::filesystem::path shaderPrevious = root / "shader-previous.txt";
    passed &= Expect(
        WriteManifest(shaderManifest, { "Rendering/Direct3D/Shaders/Mesh.hlsl" }),
        "the shader's first manifest should be written");
    const TestSupport::CommandResult shaderFirstRun =
        RunPrune(*cmake, outputDir, shaderManifest, shaderPrevious);
    passed &= Expect(
        shaderFirstRun.exitCode.value_or(-1) == 0, "staging the shader for the first time should succeed");

    const std::filesystem::path shaderManifestEmpty = root / "shader-manifest-empty.txt";
    passed &= Expect(
        WriteManifest(shaderManifestEmpty, {}), "the shader's empty manifest should be written");
    const TestSupport::CommandResult shaderSecondRun =
        RunPrune(*cmake, outputDir, shaderManifestEmpty, shaderPrevious);
    passed &= Expect(
        shaderSecondRun.exitCode.value_or(-1) == 0, "pruning the shader should succeed");
    passed &= Expect(!std::filesystem::exists(shader), "the stale shader should be gone");
    passed &= Expect(
        !std::filesystem::exists(outputDir / "Rendering"),
        "the emptied Rendering tree should be removed all the way up");
    passed &= Expect(
        std::filesystem::is_directory(outputDir),
        "OUTPUT_DIR itself should never be removed, however empty it becomes");
    passed &= Expect(
        std::filesystem::is_directory(outputDir / "Sprites"),
        "an unrelated directory beside the emptied one should be untouched");
    if (shaderSecondRun.exitCode.value_or(-1) != 0)
    {
        std::cerr << shaderSecondRun.output << "\n";
    }

    // ⒠ OUTPUT_DIR 자체가 한 번도 존재한 적 없는 채로도(정말 처음 짓는 트리) 실패하지 않아야
    // 한다 - 지울 것이 없을 뿐이다.
    const std::filesystem::path neverBuilt = root / "never-built";
    const std::filesystem::path freshManifest = root / "fresh-manifest.txt";
    const std::filesystem::path freshPrevious = root / "fresh-previous.txt";
    passed &= Expect(
        WriteManifest(freshManifest, { "Sprites/One.png" }), "the fresh manifest should be written");
    const TestSupport::CommandResult freshRun =
        RunPrune(*cmake, neverBuilt, freshManifest, freshPrevious);
    passed &= Expect(
        freshRun.exitCode.value_or(-1) == 0,
        "a never-built OUTPUT_DIR should not fail the prune");
    passed &= Expect(
        std::filesystem::exists(freshPrevious), "it should still record its memory for next time");
    if (freshRun.exitCode.value_or(-1) != 0)
    {
        std::cerr << freshRun.output << "\n";
    }

    return passed;
}

/// <summary>
/// gameengine_stage_content와 그 POST_BUILD 프루닝을 실제 configure/build로 지나가는 통합
/// 시험이다. 콘텐츠만 있는 조그만 프로젝트를 GAMEEDITOR_PROJECT_DIRECTORY로 세워 저장소
/// 전체를 configure하고 <c>&lt;프로젝트&gt;_Content</c> 타깃만 빌드한다 - 실행 파일을 링크할
/// 이유는 없고, 재는 것은 스테이징과 프루닝뿐이다.
/// </summary>
bool RunPruneStaleFilesBuildIntegrationTests()
{
    TestSupport::TemporaryDirectory workspace("prune-stale-files-build");
    const std::filesystem::path root = workspace.GetPath();
    bool passed = true;

    const std::optional<std::filesystem::path> cmake = GameEngine::Build::ResolveCMakePath({}, {});
    if (!Expect(cmake.has_value(), "cmake should be on PATH or beside Visual Studio"))
    {
        return false;
    }

    const std::filesystem::path project = root / "PruneFixture";
    passed &= Expect(
        TestSupport::WriteFile(
            project / "CMakeLists.txt", "gameengine_add_game_project(PruneFixture)\n"),
        "the fixture project's build script should be written");
    passed &= Expect(
        TestSupport::WriteFile(project / "Content" / "Sprites" / "Keep.png", "keep-bytes"),
        "the fixture's kept sprite should be written");
    passed &= Expect(
        TestSupport::WriteFile(project / "Content" / "Sprites" / "DropMe.png", "drop-bytes"),
        "the fixture's doomed sprite should be written");
    passed &= Expect(
        TestSupport::WriteFile(project / "Content" / "Sprites" / "DropMe.png.meta", "drop-meta"),
        "the fixture's doomed sidecar should be written");
    if (!passed)
    {
        return false;
    }

    const std::filesystem::path buildDir = root / "build";
    const std::filesystem::path outputRoot = root / "x64";
    const std::vector<std::string> configureArguments = {
        "-S", RepositoryRoot().string(), "-B", buildDir.string(),
        "-DGAMEENGINE_OUTPUT_ROOT=" + outputRoot.generic_string(),
        "-DGAMEEDITOR_PROJECT_DIRECTORY=" + project.generic_string() };

    const TestSupport::CommandResult firstConfigure = TestSupport::RunCommand(*cmake, configureArguments);
    passed &= Expect(
        firstConfigure.exitCode.value_or(-1) == 0,
        "a project with content-only fixtures should configure");
    if (firstConfigure.exitCode.value_or(-1) != 0)
    {
        std::cerr << firstConfigure.output.substr(0, 1500) << "\n";
        return passed;
    }

    const TestSupport::CommandResult firstBuild = TestSupport::RunCommand(
        *cmake, { "--build", buildDir.string(), "--target", "PruneFixture_Content", "--config",
                  "Debug" });
    passed &= Expect(
        firstBuild.exitCode.value_or(-1) == 0, "staging the fixture's content should succeed");
    if (firstBuild.exitCode.value_or(-1) != 0)
    {
        std::cerr << firstBuild.output.substr(0, 1500) << "\n";
        return passed;
    }

    const std::filesystem::path stagedRoot = outputRoot / "Debug" / "PruneFixture";
    const std::filesystem::path stagedKeep = stagedRoot / "Sprites" / "Keep.png";
    const std::filesystem::path stagedDropMe = stagedRoot / "Sprites" / "DropMe.png";
    const std::filesystem::path stagedDropMeMeta = stagedRoot / "Sprites" / "DropMe.png.meta";
    passed &= Expect(
        std::filesystem::exists(stagedKeep) && std::filesystem::exists(stagedDropMe) &&
            std::filesystem::exists(stagedDropMeMeta),
        "the first build should stage every content file beside the fixture's output");

    // 편집기가 실행 중에 스스로 쓰는 파일들을 흉내낸다 - 소스 글롭이 잡은 적 없는, 스테이징된
    // 것과 같은 디렉터리의 파일들이다.
    const std::filesystem::path runtimeSprite = stagedRoot / "Sprites" / "Keep.png.sprite.json";
    const std::filesystem::path runtimeSettings = stagedRoot / "PruneFixture.settings.json";
    passed &= Expect(
        TestSupport::WriteFile(runtimeSprite, "sprite-json"),
        "the simulated runtime sprite metadata should be written");
    passed &= Expect(
        TestSupport::WriteFile(runtimeSettings, "settings"),
        "the simulated runtime settings file should be written");

    // DropMe.png와 그 사이드카를 소스에서 지운다.
    passed &= Expect(
        std::filesystem::remove(project / "Content" / "Sprites" / "DropMe.png"),
        "the doomed sprite should be removable from source");
    passed &= Expect(
        std::filesystem::remove(project / "Content" / "Sprites" / "DropMe.png.meta"),
        "the doomed sidecar should be removable from source");

    const TestSupport::CommandResult secondConfigure = TestSupport::RunCommand(*cmake, configureArguments);
    passed &= Expect(
        secondConfigure.exitCode.value_or(-1) == 0, "reconfiguring after removing a file should succeed");
    if (secondConfigure.exitCode.value_or(-1) != 0)
    {
        std::cerr << secondConfigure.output.substr(0, 1500) << "\n";
        return passed;
    }

    const TestSupport::CommandResult secondBuild = TestSupport::RunCommand(
        *cmake, { "--build", buildDir.string(), "--target", "PruneFixture_Content", "--config",
                  "Debug" });
    passed &= Expect(
        secondBuild.exitCode.value_or(-1) == 0, "rebuilding after removing a file should succeed");
    if (secondBuild.exitCode.value_or(-1) != 0)
    {
        std::cerr << secondBuild.output.substr(0, 1500) << "\n";
        return passed;
    }

    passed &= Expect(
        !std::filesystem::exists(stagedDropMe) && !std::filesystem::exists(stagedDropMeMeta),
        "the rebuild should remove the staged copy of the file that lost its source, and its "
        "sidecar - this is the exact defect that let a duplicate GUID reach a shipped build");
    passed &= Expect(
        std::filesystem::exists(stagedKeep), "a content file still in source should be untouched");
    passed &= Expect(
        std::filesystem::exists(runtimeSprite) && std::filesystem::exists(runtimeSettings),
        "files the build never staged should survive the same rebuild that prunes a stale one, "
        "even though they share its directory");

    return passed;
}

static const TestSupport::Registration gPruneStaleFilesScriptTests{
    "ProjectBuilder", "prune stale files script tests should pass", RunPruneStaleFilesScriptTests };

static const TestSupport::Registration gPruneStaleFilesBuildIntegrationTests{
    "ProjectBuilder", "prune stale files build integration tests should pass",
    RunPruneStaleFilesBuildIntegrationTests };
