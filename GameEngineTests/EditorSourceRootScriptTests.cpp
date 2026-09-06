#include "EditorSourceRootScriptTests.h"

#include <filesystem>
#include <optional>
#include <string>

#include "Document/EditorContext.h"
#include "Rules/EditorFileDialogs.h"
#include "Views/EditorProjectCommands.h"
#include "TestSupport.h"

namespace
{
    class ScriptPathDialog final : public GameEditor::IFileDialogs
    {
    public:
        std::filesystem::path choice;
        std::filesystem::path initialDirectory;

        std::optional<std::filesystem::path> ShowOpen(
            const GameEngine::Platform::PlatformServices::FileDialogRequest&) override
        {
            return std::nullopt;
        }

        std::optional<std::filesystem::path> ShowSave(
            const GameEngine::Platform::PlatformServices::FileDialogRequest& request) override
        {
            initialDirectory = request.initialDirectory;
            return choice;
        }
    };
}

bool RunEditorSourceRootScriptTests()
{
    using TestSupport::Expect;
    using TestSupport::WriteFile;
    TestSupport::TemporaryDirectory temporary("editor-source-root-script");
    const std::filesystem::path source = temporary.GetPath() / "Separated";
    const std::filesystem::path content = source / "Content";
    const std::filesystem::path project = content / "Separated.gameproject";
    if (!Expect(WriteFile(project,
            R"({"projectName":"Separated","sourceRootPath":"..","initialSceneId":0,)"
            R"("window":{"width":1280,"height":720},)"
            R"("scenes":[{"id":0,"path":"Scenes/Main.scene"}]})") &&
            WriteFile(content / "Scenes/Main.scene", R"({"sceneName":"Main","gameObjects":[]})"),
            "a project with separate source and content roots should be written")) return false;

    GameEditor::EditorContext context;
    if (!Expect(context.OpenProject(project), "the separated project should open")) return false;
    ScriptPathDialog dialogs;
    dialogs.choice = source / "Source" / "NewBehaviour.h";
    GameEditor::CreateScriptWithDialog(context, dialogs);
    bool passed = Expect(dialogs.initialDirectory == source / "Source",
        "New Script should suggest the configured source root");
    passed = Expect(std::filesystem::is_regular_file(dialogs.choice) &&
        std::filesystem::is_regular_file(source / "Source/NewBehaviour.cpp"),
        "the new component should be created in the directory the project builds") && passed;
    passed = Expect(!std::filesystem::exists(content / "Source") &&
        !std::filesystem::exists(content / "CMakeLists.txt"),
        "script creation must not create a second project inside Content") && passed;
    const std::string buildScript = TestSupport::ReadFile(source / "CMakeLists.txt");
    passed = Expect(buildScript.find("CONTENT_DIR \"Content\"") != std::string::npos,
        "the generated source-root build script should stage the actual Content directory") && passed;

    const std::filesystem::path sharedRoot = temporary.GetPath() / "Shared";
    const std::filesystem::path sharedProject = sharedRoot / "Shared.gameproject";
    if (!Expect(WriteFile(sharedProject,
            R"({"projectName":"Shared","initialSceneId":0,"window":{"width":1280,"height":720},)"
            R"("scenes":[{"id":0,"path":"Scenes/Main.scene"}]})") &&
            WriteFile(sharedRoot / "Scenes/Main.scene", R"({"sceneName":"Main","gameObjects":[]})"),
            "a project with the default shared root should be written")) return false;
    if (!Expect(context.OpenProject(sharedProject), "the shared-root project should open")) return false;
    dialogs.choice = sharedRoot / "Source/SharedBehaviour.h";
    GameEditor::CreateScriptWithDialog(context, dialogs);
    passed = Expect(std::filesystem::is_regular_file(dialogs.choice) &&
        TestSupport::ReadFile(sharedRoot / "CMakeLists.txt").find("CONTENT_DIR \".\"") !=
            std::string::npos,
        "the default shared source/content root should still create scripts and stage dot") && passed;
    return passed;
}

static const TestSupport::Registration gEditorSourceRootScriptTests{
    "EditorDocument", "new scripts respect a project's source root", RunEditorSourceRootScriptTests };
