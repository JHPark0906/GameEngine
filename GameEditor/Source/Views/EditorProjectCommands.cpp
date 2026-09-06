#include "Views/EditorProjectCommands.h"

#include <optional>
#include <filesystem>

#include "Document/EditorContext.h"
#include "Rules/EditorPanelCommon.h"
#include "Views/EditorRecoveryPrompt.h"
#include "Rules/EditorMaterialTemplate.h"
#include "Rules/EditorScriptTemplate.h"
#include "App/ProjectFile.h"
#include "Assets/Asset.h"
#include "Assets/AssetDatabase.h"
#include "Core/Guid.h"
#include "Core/RelativePath.h"
#include "Diagnostics/Debug.h"

namespace GameEditor
{

namespace
{
    /// <summary>
    /// 이 장면 파일이 프로젝트에 등록된 ID다. 등록되어 있지 않거나 프로젝트 밖이면 값이 없다.
    ///
    /// ID를 여기서 다시 고르지 않고 되찾는 이유는 그것을 고르는 곳이 AddScene이기 때문이다.
    /// 경로를 프로젝트 기준으로 옮기는 규칙도 마찬가지로 여기 것이 아니다 — 그 규칙이 자리마다
    /// 조금씩 다르면 같은 파일이 자리에 따라 등록된 것도, 안 된 것도 된다.
    /// </summary>
    [[nodiscard]] std::optional<unsigned int> FindRegisteredSceneId(
        const GameEngine::App::ProjectFileData& project,
        const std::filesystem::path& scenePath)
    {
        const std::optional<std::filesystem::path> relativeScenePath =
            GameEngine::Core::RelativePathWithin(
                project.GetAssetRootPath(), std::filesystem::absolute(scenePath));
        if (!relativeScenePath)
        {
            return std::nullopt;
        }
        for (const auto& [id, registeredPath] : project.settings.scenePaths)
        {
            if (registeredPath == *relativeScenePath)
            {
                return id;
            }
        }
        return std::nullopt;
    }
}

void CreateProjectWithDialog(EditorContext& context, IFileDialogs& dialogs)
{
    GameEngine::Platform::PlatformServices::FileDialogRequest request;
    request.title = L"Create New Project";
    request.filterName = L"Game Project";
    request.extension = L".gameproject";
    request.suggestedFileName = L"NewGame.gameproject";
    if (const GameEngine::App::ProjectFileData* openProject = context.GetOpenProject())
    {
        request.initialDirectory = openProject->GetAssetRootPath();
    }
    const std::optional<std::filesystem::path> filePath =
        dialogs.ShowSave(request);
    if (!filePath)
    {
        return;
    }
    const std::optional<GameEngine::App::ProjectFileData> project =
        GameEngine::App::ProjectFile::Create(*filePath);
    if (!project)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The project could not be created. The selected directory may already contain a "
            "project or Scenes/Main.scene.");
        return;
    }
    if (!context.OpenProject(project->filePath))
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The project was created, but the editor could not open it. path=",
            project->filePath.string());
    }
}

void RenameSceneWithDialog(EditorContext& context, IFileDialogs& dialogs)
{
    const GameEngine::App::ProjectFileData* const project = context.GetOpenProject();
    const std::optional<unsigned int> sceneId = context.GetSelectedSceneId();
    if (!project || !sceneId)
    {
        GameEngine::Diagnostics::Debug::LogError("Choose a scene in the hierarchy to rename.");
        return;
    }

    GameEngine::Platform::PlatformServices::FileDialogRequest request;
    request.title = L"Rename Scene";
    request.filterName = L"Scene";
    request.extension = L".scene";
    request.initialDirectory = project->GetAssetRootPath() / "Scenes";
    const std::optional<std::filesystem::path> filePath =
        dialogs.ShowSave(request);
    if (!filePath)
    {
        return;
    }

    const std::optional<GameEngine::App::ProjectFileData> updated =
        GameEngine::App::ProjectFile::RenameScene(*project, *sceneId, *filePath);
    if (!updated)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The scene could not be renamed. A file may already exist at that path, or the path "
            "may be outside the project folder.");
        return;
    }
    ReopenProjectAtScene(context, *updated, *filePath);
}

void ReopenProjectAtScene(
    EditorContext& context, const GameEngine::App::ProjectFileData& project,
    const std::filesystem::path& scenePath)
{
    const std::optional<unsigned int> sceneId = FindRegisteredSceneId(project, scenePath);
    if (!context.OpenProject(project.filePath))
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The project could not be reopened. path=", project.filePath.string());
        return;
    }
    if (!sceneId || !context.OpenScene(*sceneId))
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The scene is listed in the hierarchy but could not be opened.");
    }
}

void CreateScriptWithDialog(EditorContext& context, IFileDialogs& dialogs)
{
    const GameEngine::App::ProjectFileData* const project = context.GetOpenProject();
    if (!project)
    {
        // 컴포넌트는 프로젝트의 코드다. 프로젝트가 없으면 놓을 자리가 없다.
        GameEngine::Diagnostics::Debug::LogError("Open a project before creating a component.");
        return;
    }
    const std::filesystem::path projectRoot = project->GetSourceRootPath();

    GameEngine::Platform::PlatformServices::FileDialogRequest request;
    request.title = L"Create New Script";
    request.filterName = L"Component source";
    request.extension = L".h";
    request.suggestedFileName = L"NewComponent.h";
    request.initialDirectory = projectRoot / "Source";
    const std::optional<std::filesystem::path> filePath =
        dialogs.ShowSave(request);
    if (!filePath)
    {
        return;
    }

    // 프로젝트가 이미 아이콘을 정해 뒀다면, 이 프로젝트가 코드를 처음 갖는 순간 함께 놓이는
    // 빌드 스크립트도 그것을 알아야 한다 — 그러지 않으면 손으로 한 줄 보태기 전까지 아이콘이
    // 없는 실행 파일이 나온다. guid를 실제 경로로 푸는 것은 지금 열려 있는 데이터베이스가
    // 안다.
    std::filesystem::path iconPath;
    if (!project->settings.icon.empty())
    {
        const std::optional<GameEngine::Core::Guid> iconGuid =
            GameEngine::Core::Guid::Parse(project->settings.icon);
        const GameEngine::Assets::AssetDatabase* const database =
            context.GetProjectAssetDatabase();
        const GameEngine::Assets::Asset* const iconAsset =
            (iconGuid && database) ? database->FindAsset(*iconGuid) : nullptr;
        if (iconAsset)
        {
            iconPath = iconAsset->GetSourcePath();
        }
        else
        {
            // 조용히 아이콘 없이 진행하기보다 말한다 — 데이터베이스가 찾지 못한 guid는
            // 사이드카가 지워졌거나 스캔 전이라는 뜻이고, 그 상태로 만든 빌드 스크립트는
            // 아이콘 없는 실행 파일을 만들면서도 이유를 남기지 않는다.
            GameEngine::Diagnostics::Debug::LogWarning(
                "The project names an icon asset that could not be found; the new build script "
                "will have none. icon=", project->settings.icon);
        }
    }

    // 고른 자리를 그대로 넘긴다. 그 자리가 쓸 수 있는 곳인지는 템플릿이 판정하고, 아니면
    // 이유를 말하고 아무것도 만들지 않는다 — 사람이 고른 곳과 다른 데 파일을 놓지 않는다.
    //
    // 코드와 콘텐츠의 루트는 다를 수 있다. 새 빌드 스크립트도 코드 루트를 기준으로 실제
    // 콘텐츠 디렉터리를 가리켜야 SampleGame 같은 분리 배치를 그대로 빌드한다.
    const std::filesystem::path assetRoot = project->GetAssetRootPath();
    const std::optional<std::filesystem::path> contentDirectory = assetRoot == projectRoot
        ? std::optional<std::filesystem::path>{ "." }
        : GameEngine::Core::RelativePathWithin(projectRoot, assetRoot);
    if (!contentDirectory)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The content directory cannot be expressed relative to the source directory.");
        return;
    }
    const std::optional<CreatedScript> created = CreateComponentScript(
        projectRoot, ToUtf8(project->settings.projectName),
        *contentDirectory, *filePath,
        iconPath, project->settings.icon);
    if (!created)
    {
        return;
    }

    // 만든 것과, 그것이 아직 보이지 않는 이유를 함께 말한다. 컴포넌트는 에디터 안에 컴파일되어
    // 들어가므로, 다시 configure하고 에디터를 다시 빌드해야 Add Component 목록에 나온다.
    GameEngine::Diagnostics::Debug::Log(
        "Created a component. header=", created->headerPath.string(),
        ", source=", created->sourcePath.string());
    if (created->wroteBuildScript)
    {
        GameEngine::Diagnostics::Debug::Log(
            "This project had no build script, so one was written in its source root.");
    }
    GameEngine::Diagnostics::Debug::LogWarning(
        "Configure and rebuild the editor to use it: cmake --preset vs "
        "-DGAMEEDITOR_PROJECT_DIRECTORY=", projectRoot.string(),
        " then cmake --build --preset debug. Until then the component exists on disk but is not "
        "in this editor.");
}

void CreateMaterialWithDialog(EditorContext& context, IFileDialogs& dialogs)
{
    const GameEngine::App::ProjectFileData* const project = context.GetOpenProject();
    if (!project)
    {
        // 머티리얼도 프로젝트의 콘텐츠다. 프로젝트가 없으면 놓을 자리가 없다.
        GameEngine::Diagnostics::Debug::LogError("Open a project before creating a material.");
        return;
    }

    GameEngine::Platform::PlatformServices::FileDialogRequest request;
    request.title = L"Create New Material";
    request.filterName = L"Material";
    request.extension = L".material";
    request.suggestedFileName = L"NewMaterial.material";
    // 다른 종류들이 자기 폴더에 모이는 것과 같은 자리다 — 장면이 Scenes/에, 스프라이트가
    // Sprites/에 모이는 것처럼, 사람이 옮기지 않는 한 머티리얼도 한자리에 모인다.
    request.initialDirectory = project->GetAssetRootPath() / "Materials";
    const std::optional<std::filesystem::path> filePath =
        dialogs.ShowSave(request);
    if (!filePath)
    {
        return;
    }

    const std::optional<std::filesystem::path> created = CreateMaterialAsset(*filePath);
    if (!created)
    {
        return;
    }

    // 만드는 것만으로 목록에 나오지 않는 이유를 말한다 — 컴포넌트와 달리 재빌드는 필요 없지만,
    // 다음 스캔까지는 여전히 보이지 않는다.
    GameEngine::Diagnostics::Debug::Log(
        "Created a material. path=", created->string(),
        ". It appears in asset pickers after the project's content is rescanned.");
}

void CreateSceneWithDialog(EditorContext& context, IFileDialogs& dialogs)
{
    const GameEngine::App::ProjectFileData* const project = context.GetOpenProject();
    if (!project)
    {
        // 장면은 프로젝트 안에 산다. 프로젝트가 없으면 어디에 등록할지가 없다.
        GameEngine::Diagnostics::Debug::LogError("Open a project before creating a scene.");
        return;
    }

    GameEngine::Platform::PlatformServices::FileDialogRequest request;
    request.title = L"Create New Scene";
    request.filterName = L"Scene";
    request.extension = L".scene";
    request.suggestedFileName = L"NewScene.scene";
    // 기본 장면이 사는 곳이라, 사람이 옮기지 않는 한 장면들이 한자리에 모인다.
    request.initialDirectory = project->GetAssetRootPath() / "Scenes";
    const std::optional<std::filesystem::path> filePath =
        dialogs.ShowSave(request);
    if (!filePath)
    {
        return;
    }

    const std::optional<GameEngine::App::ProjectFileData> updated =
        GameEngine::App::ProjectFile::AddScene(*project, *filePath);
    if (!updated)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The scene could not be created. A file may already exist at that path, or the path "
            "may be outside the project folder.");
        return;
    }

    // 방금 만든 장면의 ID를 경로로 되찾는다. AddScene이 고른 값이고, 여기서 다시 계산하면 두
    // 곳이 같은 규칙을 각자 구현하게 된다.
    const std::optional<unsigned int> newSceneId =
        FindRegisteredSceneId(*updated, *filePath);

    // 프로젝트를 다시 열어야 에디터가 새 목록을 쥔다. 계층이 읽는 것이 이 설정이기 때문이다.
    if (!context.OpenProject(updated->filePath))
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The scene was created, but the project could not be reopened. path=",
            updated->filePath.string());
        return;
    }
    if (!newSceneId || !context.OpenScene(*newSceneId))
    {
        // 만들어졌고 등록도 됐다. 열리지만 않은 것이라 계층에서 눌러 열 수 있다.
        GameEngine::Diagnostics::Debug::LogError(
            "The scene was created but could not be opened. It is listed in the hierarchy.");
    }
}

void OpenProjectWithDialog(
    EditorContext& context, IFileDialogs& dialogs, ConfirmationQueue& confirmations,
    const std::filesystem::path& recoveryDirectory)
{
    GameEngine::Diagnostics::Debug::Log("Open Project: asking for a file.");
    GameEngine::Platform::PlatformServices::FileDialogRequest request;
    request.title = L"Open Project";
    request.filterName = L"Game Project";
    request.extension = L".gameproject";
    if (const GameEngine::App::ProjectFileData* project = context.GetOpenProject())
    {
        request.initialDirectory = project->GetAssetRootPath();
    }
    const std::optional<std::filesystem::path> filePath =
        dialogs.ShowOpen(request);
    if (!filePath)
    {
        return;
    }
    GameEngine::Diagnostics::Debug::Log(
        "Open Project: opening. path=", filePath->string());
    if (!context.OpenProject(*filePath))
    {
        // 파일이 무효했을 수도, 파일은 유효한데 런타임 전환이 실패했을 수도 있다. 어느 쪽인지는
        // 콘솔 로그의 앞선 항목이 말한다.
        GameEngine::Diagnostics::Debug::LogError(
            "The project could not be opened. path=", filePath->string());
    }
    // 복구 사본이 있으면 에디터의 창으로 물어본다. 물음은 프레임을 막지 않는다.
    AskAboutRecoverySnapshots(context, confirmations, recoveryDirectory);
    GameEngine::Diagnostics::Debug::Log("Open Project: finished.");
}

}
