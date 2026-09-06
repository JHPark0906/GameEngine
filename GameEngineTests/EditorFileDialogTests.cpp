#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../GameEditor/Source/Rules/EditorConfirmation.h"
#include "../GameEditor/Source/Document/EditorContext.h"
#include "../GameEditor/Source/Rules/EditorFileDialogs.h"
#include "../GameEditor/Source/Views/EditorProjectCommands.h"
#include "Assets/AssetDatabase.h"
#include "Platform/DirectoryContentSource.h"

#include "EditorFileDialogTests.h"
#include "TestSupport.h"

using GameEditor::ConfirmationQueue;
using GameEditor::EditorContext;
using GameEditor::IFileDialogs;
using TestSupport::Expect;
using TestSupport::TemporaryDirectory;

namespace
{

/// <summary>
/// 사람 대신 답하는 대화상자다. 미리 정한 경로를 돌려주고, 빈 경로는 취소를 뜻한다.
/// 마지막 요청도 보관하여 확장자 필터와 처음 보여줄 디렉터리가 올바른지 창 없이 검사한다.
/// </summary>
class FakeFileDialogs final : public IFileDialogs
{
public:
    /// <summary>다음 물음에 돌려줄 경로다. 비어 있으면 사람이 취소한 것이다.</summary>
    std::optional<std::filesystem::path> answer;
    int openCalls = 0;
    int saveCalls = 0;
    GameEngine::Platform::PlatformServices::FileDialogRequest lastRequest;

    std::optional<std::filesystem::path> ShowOpen(
        const GameEngine::Platform::PlatformServices::FileDialogRequest& request) override
    {
        ++openCalls;
        lastRequest = request;
        return answer;
    }

    std::optional<std::filesystem::path> ShowSave(
        const GameEngine::Platform::PlatformServices::FileDialogRequest& request) override
    {
        ++saveCalls;
        lastRequest = request;
        return answer;
    }
};

[[nodiscard]] bool WriteTextFile(const std::filesystem::path& path, const std::string& contents)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        return false;
    }
    stream << contents;
    return stream.good();
}

/// <summary>장면 하나를 가진 최소 프로젝트를 만든다. 경로는 .gameproject 파일이다.</summary>
[[nodiscard]] std::filesystem::path WriteTestProject(const std::filesystem::path& root)
{
    const std::filesystem::path projectFile = root / "DialogTest.gameproject";
    const bool wrote =
        WriteTextFile(projectFile,
            R"({"projectName": "DialogTest",)"
            R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
            R"( "initialSceneId": 0, "scenes": [)"
            R"( { "id": 0, "path": "Scenes/First.scene" } ]})") &&
        WriteTextFile(root / "Scenes" / "First.scene",
            R"({"sceneName": "First", "gameObjects": []})");
    return wrote ? projectFile : std::filesystem::path{};
}

/// <summary>그 디렉터리 아래에 있는 보통 파일의 수다. 취소가 아무것도 남기지 않았는지 센다.</summary>
[[nodiscard]] std::size_t CountFiles(const std::filesystem::path& root)
{
    std::size_t count = 0;
    std::error_code error;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(root, error))
    {
        if (entry.is_regular_file(error))
        {
            ++count;
        }
    }
    return count;
}

}

bool RunEditorFileDialogTests()
{
    std::cout << "running editor file dialog tests\n";

    TemporaryDirectory temporaryDirectory("editor-file-dialog");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::filesystem::path projectFile = WriteTestProject(root);
    if (!Expect(!projectFile.empty(), "the dialog test project should be written"))
    {
        return false;
    }

    bool passed = true;
    ConfirmationQueue confirmations;
    // 사본이 사는 자리도 이 시험의 임시 디렉터리 안이다. 편집기의 진짜 임시 자리를
    // 주면 이 시험이 사용자의 사본을 훑게 되고, 지우는 답변이 한 줄 더해지는 날 그것을 지운다.
    const std::filesystem::path recoveryDirectory = root / "Recovery";

    // ---- Open Project ----
    {
        EditorContext context;
        FakeFileDialogs dialogs;

        // 취소. 아무 프로젝트도 열리지 않아야 한다.
        dialogs.answer.reset();
        OpenProjectWithDialog(context, dialogs, confirmations, recoveryDirectory);
        passed = Expect(dialogs.openCalls == 1, "Open Project should ask once") && passed;
        passed = Expect(
            context.GetOpenProject() == nullptr,
            "cancelling Open Project should leave no project open") && passed;

        // 고른다.
        dialogs.answer = projectFile;
        OpenProjectWithDialog(context, dialogs, confirmations, recoveryDirectory);
        passed = Expect(
            context.GetOpenProject() != nullptr && context.HasOpenScene(),
            "choosing a project should open it and its initial scene") && passed;
        passed = Expect(
            dialogs.lastRequest.extension == L".gameproject",
            "Open Project should ask for a project file") && passed;
    }

    // ---- New Scene ----
    {
        EditorContext context;
        FakeFileDialogs dialogs;
        passed = Expect(context.OpenProject(projectFile), "the project should open") && passed;

        const std::filesystem::path scenePath = root / "Scenes" / "Added.scene";
        dialogs.answer.reset();
        CreateSceneWithDialog(context, dialogs);
        passed = Expect(
            !std::filesystem::exists(scenePath),
            "cancelling New Scene should write no scene file") && passed;

        dialogs.answer = scenePath;
        CreateSceneWithDialog(context, dialogs);
        passed = Expect(
            std::filesystem::exists(scenePath),
            "choosing a path for New Scene should write the scene there") && passed;
        const GameEngine::App::ProjectFileData* const project = context.GetOpenProject();
        passed = Expect(
            project != nullptr && project->settings.scenePaths.size() == 2,
            "the new scene should be registered in the project, not only written to disk")
            && passed;
        // 계층이 읽는 것은 디렉터리가 아니라 이 목록이다. 파일만 생기고 등록되지 않으면
        // 만들어졌는데 보이지 않는 장면이 된다.
    }

    // ---- Rename Scene ----
    {
        EditorContext context;
        FakeFileDialogs dialogs;
        passed = Expect(context.OpenProject(projectFile), "the project should open") && passed;
        context.SelectScene(0);

        const std::filesystem::path renamed = root / "Scenes" / "Renamed.scene";
        const std::filesystem::path original = root / "Scenes" / "First.scene";
        dialogs.answer.reset();
        RenameSceneWithDialog(context, dialogs);
        passed = Expect(
            std::filesystem::exists(original) && !std::filesystem::exists(renamed),
            "cancelling Rename Scene should leave the scene where it was") && passed;

        dialogs.answer = renamed;
        RenameSceneWithDialog(context, dialogs);
        passed = Expect(
            std::filesystem::exists(renamed) && !std::filesystem::exists(original),
            "choosing a name should move the scene file to it") && passed;

        // 되돌려 둔다. 아래 시험들이 같은 프로젝트를 다시 쓴다.
        std::error_code error;
        std::filesystem::rename(renamed, original, error);
        passed = Expect(!error, "the renamed scene should move back for the next test") && passed;
    }

    // ---- New Script ----
    {
        EditorContext context;
        FakeFileDialogs dialogs;
        passed = Expect(context.OpenProject(projectFile), "the project should open") && passed;

        const std::filesystem::path header = root / "Source" / "Made.h";
        dialogs.answer.reset();
        CreateScriptWithDialog(context, dialogs);
        passed = Expect(
            !std::filesystem::exists(header),
            "cancelling New Script should write no header") && passed;

        dialogs.answer = header;
        CreateScriptWithDialog(context, dialogs);
        passed = Expect(
            std::filesystem::exists(header),
            "choosing a path for New Script should write the header there") && passed;
        passed = Expect(
            std::filesystem::exists(root / "Source" / "Made.cpp"),
            "New Script should write the source beside the header") && passed;
    }

    // ---- New Material ----
    {
        EditorContext context;
        FakeFileDialogs dialogs;
        passed = Expect(context.OpenProject(projectFile), "the project should open") && passed;

        const std::filesystem::path materialPath = root / "Materials" / "Made.material";
        dialogs.answer.reset();
        CreateMaterialWithDialog(context, dialogs);
        passed = Expect(
            !std::filesystem::exists(materialPath),
            "cancelling New Material should write no file") && passed;
        passed = Expect(
            dialogs.lastRequest.extension == L".material",
            "New Material should ask for a .material file") && passed;

        dialogs.answer = materialPath;
        CreateMaterialWithDialog(context, dialogs);
        passed = Expect(
            std::filesystem::exists(materialPath),
            "choosing a path for New Material should write the file there") && passed;

        // 만든 파일이 정말 최소 머티리얼로 읽히는지, 텍스트가 아니라 진짜 임포터로 잰다 —
        // 바이트가 있다는 것과 그것이 텍스처 없음+흰색 tint로 파싱된다는 것은 다른 주장이다.
        const GameEngine::Platform::DirectoryContentSource content(root);
        GameEngine::Assets::AssetDatabase database;
        if (Expect(database.Refresh(content), "the project should refresh after New Material"))
        {
            const std::vector<GameEngine::Assets::AssetChoice> choices =
                GameEngine::Assets::CollectAssetChoices(
                    database, GameEngine::Assets::AssetType::Material);
            const auto found = std::ranges::find_if(
                choices,
                [](const GameEngine::Assets::AssetChoice& choice)
                { return choice.label.find("Made.material") != std::string::npos; });
            passed = Expect(
                found != choices.end(),
                "the new material should be findable by its own type after a rescan") && passed;
            if (found != choices.end())
            {
                const std::shared_ptr<const GameEngine::Assets::MaterialData> material =
                    database.LoadMaterial(found->reference);
                passed = Expect(material != nullptr, "the new material should load") && passed;
                passed = Expect(
                    material && material->tint == GameEngine::Math::Color(1.0f, 1.0f, 1.0f, 1.0f),
                    "a freshly created material should default to white") && passed;
                passed = Expect(
                    material && !material->texture.IsValid(),
                    "a freshly created material should start with no texture") && passed;
            }
        }
    }

    // ---- New Project ----
    {
        EditorContext context;
        FakeFileDialogs dialogs;
        const std::filesystem::path madeRoot = root / "Made";
        std::error_code error;
        std::filesystem::create_directories(madeRoot, error);
        const std::size_t before = CountFiles(madeRoot);

        dialogs.answer.reset();
        CreateProjectWithDialog(context, dialogs);
        passed = Expect(
            CountFiles(madeRoot) == before,
            "cancelling New Project should write nothing") && passed;
        passed = Expect(
            context.GetOpenProject() == nullptr,
            "cancelling New Project should leave no project open") && passed;

        const std::filesystem::path madeFile = madeRoot / "Made.gameproject";
        dialogs.answer = madeFile;
        CreateProjectWithDialog(context, dialogs);
        passed = Expect(
            std::filesystem::exists(madeFile),
            "choosing a path for New Project should write the project there") && passed;
        passed = Expect(
            context.GetOpenProject() != nullptr,
            "a project that was just created should be the open one") && passed;
    }

    return passed;
}

static const TestSupport::Registration gEditorFileDialogTests{
    "EditorDocument", "editor file dialog tests should pass", RunEditorFileDialogTests };
