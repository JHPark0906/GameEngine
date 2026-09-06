#include "PackagingIdentityFlagTests.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "Build/ProjectBuilder.h"
#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::ReadFile;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{
    /// <summary>프로젝트 안의 사이드카 수다. 거부가 파일을 만들지 않았는지 이것으로 잰다.</summary>
    [[nodiscard]] std::size_t CountSidecars(const std::filesystem::path& contentRoot)
    {
        std::size_t count = 0;
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator iterator(contentRoot, error),
            end; iterator != end; iterator.increment(error))
        {
            if (error)
            {
                break;
            }
            if (iterator->is_regular_file() && iterator->path().extension() == ".meta")
            {
                ++count;
            }
        }
        return count;
    }
}

bool RunPackagingIdentityFlagTests()
{
    TemporaryDirectory temporaryDirectory("packaging-identity-flag");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::filesystem::path contentRoot = root / "Project" / "Content";
    const std::filesystem::path compiledRoot = root / "Compiled";

    constexpr std::string_view settings = R"({
  "projectName": "FlagTest",
  "window": { "width": 1280, "height": 720 },
  "targetFrameRate": 60,
  "initialSceneId": 0,
  "scenes": [ { "id": 0, "path": "Scenes/Main.scene" } ],
  "graphicsApi": "Auto"
})";

    // 하나만 정체성을 갖고 나머지는 갖지 않는다. 편집기가 한 번도 열지 않은 프로젝트의 모습이다.
    const std::string keptGuid = "0a1b2c3d4e5f60718293a4b5c6d7e8f9";
    const std::string kept = R"({"format":"gameengine-meta/1","guid":")" + keptGuid +
        R"(","pixelsPerUnit":50.0})";
    bool wrote =
        WriteFile(contentRoot / "FlagTest.gameproject", settings) &&
        WriteFile(contentRoot / "Scenes/Main.scene", "{}") &&
        WriteFile(contentRoot / "Textures/Albedo.png", "png") &&
        WriteFile(contentRoot / "Textures/Albedo.png.meta", kept) &&
        WriteFile(contentRoot / "Audio/Click.wav", "audio") &&
        WriteFile(compiledRoot / "FlagTest.exe", "executable") &&
        WriteFile(compiledRoot / "Rendering/Direct3D/Shaders/Mesh.hlsl", "mesh") &&
        WriteFile(compiledRoot / "Rendering/Direct3D/Shaders/Sprite.hlsl", "sprite") &&
        WriteFile(compiledRoot / "Rendering/Direct3D/Shaders/Text.hlsl", "text") &&
        WriteFile(compiledRoot / "Rendering/Direct3D/Shaders/SkinnedMesh.hlsl", "skinned mesh");
    if (!Expect(wrote, "the packaging flag test project should be written"))
    {
        return false;
    }

    const auto build = [&](const std::filesystem::path& outputPath, const bool issue)
    {
        const GameEngine::Build::ProjectBuildRequest request{
            contentRoot,
            compiledRoot / "FlagTest.exe",
            compiledRoot,
            outputPath,
            false,
            issue
        };
        return GameEngine::Build::ProjectBuilder::Build(request);
    };

    // 🔴 플래그 없이는 거부하고, 프로젝트에 파일을 하나도 만들지 않는다. 빌드를 한 번 돌렸다고
    // 저장소에 새 파일이 생기지 않아야 한다는 것이 이 기본값의 이유다.
    const std::optional<GameEngine::Build::ProjectBuildResult> refused =
        build(root / "Build" / "Refused", false);
    const std::size_t sidecarsAfterRefusal = CountSidecars(contentRoot);
    const bool keptSurvivedRefusal = ReadFile(contentRoot / "Textures/Albedo.png.meta") == kept;

    // 플래그가 있으면 발급하고 패키징까지 간다.
    const std::optional<GameEngine::Build::ProjectBuildResult> issued =
        build(root / "Build" / "Issued", true);
    const bool issuedEverything =
        std::filesystem::is_regular_file(contentRoot / "Scenes/Main.scene.meta") &&
        std::filesystem::is_regular_file(contentRoot / "Audio/Click.wav.meta") &&
        std::filesystem::is_regular_file(contentRoot / "FlagTest.gameproject.meta");

    // 🔴 어느 쪽에서도 이미 있는 정체성은 덮지 않는다. 다시 발급하면 그것을 가리키던 참조가
    // 전부 아무것도 가리키지 않게 된다.
    const bool keptSurvivedIssue = ReadFile(contentRoot / "Textures/Albedo.png.meta") == kept;

    return Expect(!refused, "packaging without the flag should refuse a project missing identities") &&
        Expect(sidecarsAfterRefusal == 1, "a refused packaging should create no files in the project") &&
        Expect(keptSurvivedRefusal, "a refused packaging should leave a recorded identity untouched") &&
        Expect(issued.has_value(), "packaging with the flag should issue identities and succeed") &&
        Expect(issuedEverything, "every asset that had no identity should have received one") &&
        Expect(keptSurvivedIssue, "issuing should leave an identity already recorded byte for byte");
}

static const TestSupport::Registration gPackagingIdentityFlagTests{
    "ProjectBuilder", "packaging identity flag tests should pass", RunPackagingIdentityFlagTests };
