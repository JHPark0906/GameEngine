#include "Document/EditorContext.h"


#include <fstream>
#include <iterator>
#include <system_error>

#include "Assets/AssetDatabase.h"
#include "Assets/AssetIdentityIssue.h"
#include "Assets/AssetMoveMatching.h"
#include "Core/TextFile.h"
#include "Diagnostics/Debug.h"
#include "Runtime/Game.h"
#include "Runtime/Object.h"
#include "Platform/IAudioOutput.h"
#include "Rendering/CachedTextMeasure.h"
#include "Rendering/TextRasterizationCache.h"
#include "Platform/PlatformServices.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Serialization/ComponentSchema.h"
#include "Serialization/SceneSerializer.h"
#include "Rules/EditorFonts.h"
#include "Rules/EditorRecovery.h"

#include <cstddef>
#include <algorithm>
#include <array>
#include <ctime>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace GameEditor
{


EditorContext::EditorContext()
    : EditorContext(GameEngine::Platform::PlatformServices::GetExecutableDirectory(),
          EditorSettingsStore::GetDefaultFilePath())
{
}

EditorContext::EditorContext(const std::filesystem::path& editorContentRoot,
    std::filesystem::path settingsFilePath)
    : mSettingsStore(std::move(settingsFilePath))
{
    // 에디터 자신의 에셋은 프로젝트가 열려 런타임이 다른 곳을 가리키게 된 뒤에도 계속
    // 필요하다. 기본 생성자는 배포 경로를 주고, 여기서는 그 독립 데이터베이스를 세운다.
    mEditorContent = std::make_unique<GameEngine::Platform::DirectoryContentSource>(
        editorContentRoot);
    mEditorAssetDatabase = std::make_unique<GameEngine::Assets::AssetDatabase>();
    if (!mEditorContent->IsValid() || !mEditorAssetDatabase->Refresh(*mEditorContent))
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The editor could not read its own deployed assets.");
    }

}

EditorContext::~EditorContext() = default;

std::filesystem::path EditorContext::GetSettingsFilePath()
{
    return EditorSettingsStore::GetDefaultFilePath();
}

void EditorContext::UpdatePanelLayoutSetting(std::vector<std::size_t> panelInSlot)
{
    mSettingsStore.UpdatePanelLayout(std::move(panelInSlot));
}

void EditorContext::UpdateConsoleWindowSetting(
    const bool floating, const float x, const float y, const float width, const float height)
{
    mSettingsStore.UpdateConsoleWindow(floating, x, y, width, height);
}

void EditorContext::SetGridSnapEnabled(const bool enabled)
{
    mSettingsStore.SetGridSnapEnabled(enabled);
}

void EditorContext::UpdateSceneCameraSetting(
    const GameEngine::Math::Vector3& pivot, const float distance, const float yawDegrees,
    const float pitchDegrees)
{
    mSettingsStore.UpdateSceneCamera(pivot, distance, yawDegrees, pitchDegrees);
}

GameEngine::Runtime::Object* EditorContext::FindObject(const unsigned int instanceId) const
{
    return mDocument.FindObject(MakeDocumentHandles(), instanceId);
}

GameEngine::Runtime::Component* EditorContext::FindComponent(const unsigned int instanceId)
{
    // 객체 종류를 판별하는 캐스트는 여기서 수행한다.
    return dynamic_cast<GameEngine::Runtime::Component*>(FindObject(instanceId));
}

GameEngine::Runtime::GameObject* EditorContext::FindGameObject(const unsigned int instanceId)
{
    return dynamic_cast<GameEngine::Runtime::GameObject*>(FindObject(instanceId));
}

unsigned int EditorContext::ResolveObjectId(const unsigned int instanceId) const
{
    return mDocument.ResolveObjectId(instanceId);
}

void EditorContext::RecordObjectIdAlias(const unsigned int oldId, const unsigned int newId)
{
    mDocument.RecordObjectIdAlias(oldId, newId);
}

void EditorContext::ResetUndoHistory()
{
    mDocument.ResetUndoHistory();
}

void EditorContext::RecordEdit(std::unique_ptr<GameEngine::Core::IEditCommand> command)
{
    mDocument.RecordEdit(std::move(command));
}
void EditorContext::LoadGameComponentSchemas(const std::filesystem::path& projectRoot)
{
    namespace fs = std::filesystem;
    mGameComponentSchemas.clear();

    const fs::path schemaPath = projectRoot / GameEngine::Serialization::ComponentSchemaFileName;
    std::error_code error;
    if (!fs::is_regular_file(schemaPath, error))
    {
        // 이 메시지가 이 기능의 절반이다. 스키마가 없으면 게임 컴포넌트는 데이터로만 실려
        // 오는데, 그것을 조용히 빈 목록으로 보이면 사람은 "왜 내 컴포넌트가 없지"에서 멈춘다.
        // 무엇이 없고 무엇을 하면 생기는지 말한다.
        GameEngine::Diagnostics::Debug::LogWarning(
            "No component schema beside this project, so components this editor cannot create stay "
            "read-only data. Build the game project once to write it. expected=",
            schemaPath.string());
        return;
    }

    const std::optional<std::string> text = GameEngine::Core::ReadTextFile(schemaPath);
    if (!text)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The component schema could not be read. path=", schemaPath.string());
        return;
    }
    mGameComponentSchemas = GameEngine::Serialization::ParseComponentSchemas(*text);

    // 낡은 스키마는 없는 스키마보다 나쁘다: 있는 것을 보여주지만 그것이 지금의 코드가 아니다.
    // 프로젝트 옆의 실행 파일이 스키마보다 새로우면 그 사실을 말한다 — 게임을 고치고 다시
    // 빌드하지 않은 채 에디터로 온 경우가 그것이다.
    const fs::file_time_type schemaTime = fs::last_write_time(schemaPath, error);
    if (!error)
    {
        for (const auto& entry : fs::directory_iterator(projectRoot, error))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".exe")
            {
                continue;
            }
            std::error_code entryError;
            const fs::file_time_type executableTime =
                fs::last_write_time(entry.path(), entryError);
            if (!entryError && executableTime > schemaTime)
            {
                GameEngine::Diagnostics::Debug::LogWarning(
                    "The component schema is older than the game executable beside it, so it may "
                    "describe older components. Build the game project to refresh it. schema=",
                    schemaPath.string(), ", executable=", entry.path().filename().string());
                break;
            }
        }
    }

    GameEngine::Diagnostics::Debug::Log(
        "Loaded the project's component schema. types=", mGameComponentSchemas.size(),
        ", path=", schemaPath.string());
}

const GameEngine::Serialization::ComponentSchema* EditorContext::FindGameComponentSchema(
    const std::string_view typeName) const
{
    for (const GameEngine::Serialization::ComponentSchema& schema : mGameComponentSchemas)
    {
        if (schema.typeName == typeName)
        {
            return &schema;
        }
    }
    return nullptr;
}

GameEngine::Runtime::Object* EditorContext::GetSelectedObject() const
{
    return mDocument.GetSelectedObject(MakeDocumentHandles());
}
bool EditorContext::OpenProject(const std::filesystem::path& projectFilePath)
{
    std::optional<GameEngine::App::ProjectFileData> project =
        GameEngine::App::ProjectFile::Load(projectFilePath);
    if (!project)
    {
        return false;
    }
    // 에셋 루트의 유효성은 콘텐츠 루트를 정의하는 에셋 데이터베이스가 판정한다.
    // 여기에 같은 검사를 두면 실패 이유를 서로 다른 곳에서 관리하게 된다.
    const std::filesystem::path projectRoot = project->GetAssetRootPath();
    for (const auto& [sceneId, relativeScenePath] : project->settings.scenePaths)
    {
        if (relativeScenePath.empty() || relativeScenePath.is_absolute() ||
            std::ranges::any_of(relativeScenePath, [](const std::filesystem::path& part)
            {
                return part == "..";
            }))
        {
            GameEngine::Diagnostics::Debug::LogError(
                "Project contains an invalid scene path. sceneId=", sceneId,
                ", path=", relativeScenePath.string());
            return false;
        }

        std::error_code error;
        const std::filesystem::path scenePath =
            std::filesystem::weakly_canonical(projectRoot / relativeScenePath, error);
        const std::filesystem::path relativePath =
            std::filesystem::relative(scenePath, projectRoot, error);
        // 프로젝트 밖을 가리키는 등록은 거부한다. 이것은 사람이 고칠 상태가 아니라 잘못된
        // 프로젝트다: 경로가 프로젝트 기준 상대 경로로 배포되므로, 밖을 가리키면 빌드된
        // 게임에 그 파일이 없다. 열어서 보여 줄 것이 아니라 거절할 것이다.
        if (error || relativePath.empty() || relativePath.is_absolute() ||
            std::ranges::any_of(relativePath, [](const std::filesystem::path& part)
            {
                return part == "..";
            }))
        {
            GameEngine::Diagnostics::Debug::LogError(
                "Project scene is outside the project directory. sceneId=", sceneId,
                ", path=", relativeScenePath.string());
            return false;
        }

        // 파일이 없는 것은 다르다. 그것은 사람이 만들 수 있는 상태이고 — 탐색기에서 장면을
        // 지우면 그렇게 된다 — 그 상태로 프로젝트를 거절하면 <b>에디터가 만든 상태를 에디터로
        // 고칠 수 없게 된다.</b> 장면을 지우는 기능이 있는데 지운 뒤에는 열 수가 없다.
        //
        // 그래서 시작 장면을 못 열었을 때와 같은 모양으로 답한다: 프로젝트는 열리고, 그 사실과
        // 경로가 사람이 읽는 곳에 나온다. 계층은 그 행을 "파일 없음"으로 그리고, 거기서 지우거나
        // 이름을 바꿔 고칠 수 있다.
        std::error_code existenceError;
        if (!std::filesystem::is_regular_file(scenePath, existenceError) || existenceError)
        {
            GameEngine::Diagnostics::Debug::LogError(
                "A registered scene file is missing, so that scene is listed but cannot be "
                "opened. Delete it from the Hierarchy, or put the file back. sceneId=", sceneId,
                ", path=", relativeScenePath.string());
        }
    }

    // 에디터가 여는 프로젝트는 디스크의 디렉터리이므로 소스를 만들어 보관한다. 프로젝트가 열려
    // 있는 동안 런타임이 이것을 통해 읽는다. 데이터베이스는 만들지 않는다 — 아래에서 런타임을
    // 이 프로젝트 위에 다시 세울 때 런타임의 데이터베이스가 전체 스캔을 한 번 하고, 브라우저는
    // 그것을 읽는다. 사본 데이터베이스를 따로 두면 같은 스캔을 두 번 하게 된다.
    auto projectContent =
        std::make_unique<GameEngine::Platform::DirectoryContentSource>(projectRoot);
    if (!projectContent->IsValid())
    {
        return false;
    }
    for (const auto& [sceneId, relativeScenePath] : project->settings.scenePaths)
    {
        // 장면 파일이 장면 에셋으로 임포트되는 확장자인지는 스캔 없이도 답할 수 있는 질문이다.
        if (GameEngine::Assets::AssetDatabase::GetAssetType(relativeScenePath) !=
            GameEngine::Assets::AssetType::Scene)
        {
            GameEngine::Diagnostics::Debug::LogError(
                "Project scene is not a Scene asset. sceneId=", sceneId,
                ", path=", relativeScenePath.string());
            return false;
        }
    }

    // 이 런타임의 장면은 엔진 루프가 가진 장면 프론트엔드가 그리고, 그 프론트엔드는 자기
    // sceneTextCache로 그린다(EditorBootstrap::RegisterSceneFonts가 등록한 폰트로) — 그 캐시는
    // 여기서 닿을 수 없다. 그래서 재는 캐시와 그리는 캐시는 여전히 둘이다. 갈라지지 않게 하는
    // 것은 <b>같은 폰트를 같은 순서로 여기에도 등록하는 것</b>이다 — 등록하는 캐시가 둘이어도
    // 그 안의 폰트가 같으면 같은 문자열이 같은 폭으로 잡힌다. 하나로 합치려면 루프가 자기
    // 캐시를 내주어야 하고, 그것은 이 단위의 밖이다.
    auto projectTextCache = std::make_shared<GameEngine::Rendering::TextRasterizationCache>(
        GameEngine::Platform::PlatformServices::CreateTextRasterizer());
    RegisterSceneFonts(*mEditorContent, *projectTextCache);

    // 프로젝트마다 런타임을 새로 세운다. 이전 런타임은 이전 소스가 아직 살아 있는 동안
    // 파괴되어야 하므로, 이전 소스는 새 런타임이 선 뒤에 놓는다.
    auto projectGame = std::make_unique<GameEngine::Runtime::Game>(
        GameEngine::Platform::PlatformServices::CreateAudioOutput(),
        std::make_unique<GameEngine::Rendering::CachedTextMeasure>(projectTextCache));
    projectGame->SetSceneLoader(GameEngine::Serialization::SceneSerializer::MakeSceneLoader());

    // 런타임에는 <b>파일이 있는 장면만</b> 준다. 등록 전체는 프로젝트 설정에 그대로 남아 있고,
    // 계층이 읽는 것이 그쪽이라 없는 장면도 "파일 없음"으로 보인다 — 보여야 사람이 그것을
    // 지우거나 고칠 수 있다.
    //
    // 런타임 쪽을 느슨하게 만들지 않는 이유는 그 코드가 플레이어와 공유되기 때문이다. 배포된
    // 게임에 장면 파일이 없는 것은 사람이 고칠 상태가 아니라 망가진 빌드이고, 거기서는 조용히
    // 하나 빠진 채로 도는 것보다 크게 실패하는 편이 낫다. 에디터에서만 다른 것은 <b>여기서 무엇을
    // 건네는가</b>이지 저쪽의 규칙이 아니다.
    std::unordered_map<unsigned int, std::filesystem::path> runnableScenePaths;
    for (const auto& [sceneId, relativeScenePath] : project->settings.scenePaths)
    {
        std::error_code presenceError;
        const std::filesystem::path scenePath =
            (projectRoot / relativeScenePath).lexically_normal();
        if (std::filesystem::is_regular_file(scenePath, presenceError) && !presenceError)
        {
            runnableScenePaths.emplace(sceneId, relativeScenePath);
        }
    }
    if (!projectGame->Initialize(*projectContent, runnableScenePaths))
    {
        // GetAssetRootPath는 프로젝트 파일이 있는 자리를 답한다.
        // 실패 이유는 장면 로드 등 실제로 실패한 경로에서 각각 기록한다.
        GameEngine::Diagnostics::Debug::LogError(
            "Failed to build a runtime for the opened project. file=",
            project->filePath.string());
        return false;
    }

    // 이전 문서는 여기서 통째로 닫힌다. 무엇을 비우는지는 문서가 알고, 이 자리는 「닫아라」만
    // 말한다 — 문서에 상태가 하나 늘어도 이 줄은 그대로다.
    mDocument.Close();
    mProjectGame.reset();
    // 루트의 변동을 듣기 시작한다. 프로젝트마다 새로 만든다: 감시는 디렉터리 하나에 붙고, 이전
    // 프로젝트의 눈은 여기서 놓인다.
    mProjectWatch.Watch(
        GameEngine::Platform::PlatformServices::CreateDirectoryWatcher(projectRoot));
    mProjectContent = std::move(projectContent);
    mProjectGame = std::move(projectGame);
    mOpenProject = std::move(project);
    ++mProjectRevision;

    // 열자마자 정체성 없는 에셋에 하나씩 발급한다. 프로젝트가 처음 열리는 순간이 그럴 유일한
    // 기회는 아니지만 — 감시자가 새 파일마다 다시 부른다 — 이미 있던 에셋들은 여기서만 채워진다.
    // 발급한 것이 있으면 다시 읽는다: 그러지 않으면 방금 준 정체성을 이 데이터베이스가 모른 채로
    // 편집이 시작되고, 패키징이 그 에셋을 정체성 없는 것으로 본다.
    // 옆으로 치워 둔 사이드카의 주인이 돌아왔으면 먼저 이름을 되돌린다. 발급보다 앞이라야
    // 돌아온 파일이 새 정체성을 받기 전에 옛 정체성을 되찾는다.
    // 두 호출을 한 식에 두지 않는다. + 의 피연산자 평가 순서는 정해져 있지 않아서, 발급이 먼저
    // 돌면 돌아온 파일이 새 정체성을 받아 버린다 — 되돌리기가 지키려던 바로 그것을 잃는다.
    const std::size_t restored = RestoreSetAsideSidecars();
    if (restored + WriteMissingSidecars() > 0)
    {
        GameEngine::Assets::AssetDatabase reread;
        if (reread.Refresh(*mProjectContent))
        {
            mProjectGame->GetAssetDatabase() = std::move(reread);
        }
    }

    // 게임 빌드가 적은 스키마를 읽어, 이 프로세스에 등록되지 않은 컴포넌트도
    // 속성과 기본값을 편집할 수 있게 한다.
    LoadGameComponentSchemas(mOpenProject->GetAssetRootPath());

    // 장면이 에셋을 어떻게 가리키고 있는지 여기서 본다. 정체성 발급 뒤인 것이 중요하다 — 그
    // 전에 물으면 아직 정체성이 없는 에셋 때문에 늘 막힌다.
    SurveySceneMigration();

    // 그다음이 정리다. 이관이 끝나야 「장면이 이 정체성을 가리키는가」가 정확해지고, 그 답이
    // 무엇을 치우면 안 되는지를 정한다.
    SurveyOrphanSidecars();

    // 다음 부팅이 여기로 돌아온다. 장면 id는 아래 OpenScene의 성공이 적는다.
    mSettingsStore.RememberLastProject(mOpenProject->filePath);

    GameEngine::Diagnostics::Debug::Log(
        "Editor opened project. file=", mOpenProject->filePath.string(),
        ", assets=", mProjectGame->GetAssetDatabase().GetAssets().size());

    // 프로젝트가 시작 장면이라 말한 것을 곧바로 편집 대상으로 연다. 실패해도 프로젝트는 열린
    // 상태로 남는다 — 다른 장면을 계층 창에서 열 수 있다.
    const unsigned int initialSceneId = mOpenProject->settings.initialSceneId;
    if (!OpenScene(initialSceneId))
    {
        // 실패의 원인은 OpenScene이 이미 적었다. 여기서 적는 것은 그 원인이 사람에게 어떤
        // 모습으로 오는가다: 툴바에는 프로젝트 이름이 뜨는데 계층이 비어 있고, `Add`를 눌러도
        // 아무 일이 없다 — 장면이 없으니 그것은 정상 동작이다. 그 화면만 보고는 무엇이
        // 잘못됐는지 알 길이 없어서, 원인과 결과를 한 줄로 이어 둔다.
        //
        // 이 실패로 프로젝트 열기를 false로 돌려보내지 않는다. 프로젝트는 실제로 열렸고,
        // 그렇게 바꾸면 "프로젝트가 안 열린다"로 증상만 옮겨 갈 뿐 원인은 여전히 보이지 않는다.
        const auto initialScenePath = mOpenProject->settings.scenePaths.find(initialSceneId);
        const std::string initialScenePathText =
            initialScenePath == mOpenProject->settings.scenePaths.end()
                ? std::string("<not registered in the project>")
                : initialScenePath->second.string();
        GameEngine::Diagnostics::Debug::LogError(
            "The project opened but its initial scene did not, so no scene is open. Open another "
            "scene from the Hierarchy, or repair this one. sceneId=", initialSceneId,
            ", path=", initialScenePathText);
    }
    return true;
}

bool EditorContext::OpenScene(const unsigned int projectSceneId)
{
    if (!mDocument.OpenScene(MakeDocumentHandles(), projectSceneId))
    {
        return false;
    }
    // 올린 뒤에야 이 둘을 한다. 올리지 못한 장면이 다음 부팅의 장면이 되거나 리비전을 올리면,
    // 다음 실행이 열리지 않는 장면으로 돌아가고 화면을 보는 것들이 바뀌지 않은 것을 다시 읽는다.
    mSettingsStore.RememberLastScene(projectSceneId);
    ++mProjectRevision;
    return true;
}

EditorSceneDocument::ProjectHandles EditorContext::MakeDocumentHandles() const
{
    return { mOpenProject ? &*mOpenProject : nullptr, mProjectGame.get(), mProjectContent.get() };
}
const GameEngine::Assets::AssetDatabase* EditorContext::GetProjectAssetDatabase() const
{
    return mOpenProject && mProjectGame ? &mProjectGame->GetAssetDatabase() : nullptr;
}

void EditorContext::PollProjectAssetChanges()
{
    // 다시 읽을 수 있는지는 감시의 물음이 아니라 다시 읽기의 전제라 여기서 본다.
    if (!mProjectGame || !mProjectContent)
    {
        return;
    }
    if (mProjectWatch.PollForQuietChange(std::chrono::steady_clock::now()))
    {
        static_cast<void>(RefreshProjectAssets());
    }
}

std::size_t EditorContext::WriteMissingSidecars()
{
    return mAssetMaintenance.WriteMissingSidecars(MakeProjectHandles());
}

std::size_t EditorContext::MoveSidecarsForMovedAssets(
    const GameEngine::Assets::AssetDatabase& rescanned)
{
    return mAssetMaintenance.MoveSidecarsForMovedAssets(MakeProjectHandles(), rescanned);
}

std::unique_ptr<GameEngine::Runtime::Scene> EditorContext::LoadSceneForMigration(
    const std::filesystem::path& relativeScenePath) const
{
    return mAssetMaintenance.LoadSceneForMigration(MakeProjectHandles(), relativeScenePath);
}

void EditorContext::SurveySceneMigration()
{
    mAssetMaintenance.SurveySceneMigration(MakeProjectHandles());
}

bool EditorContext::ApplySceneMigration()
{
    // 전제는 여기서 본다. 이관은 열린 장면의 파일도 다시 쓰고 그 장면을 다시 읽으므로,
    // 저장하지 않은 편집이 있는 채로 하면 그 편집이 사라진다. 부품은 문서를 모르니 이 물음에
    // 답할 수 없고, 답할 수 있는 자리는 여기뿐이다.
    if (mDocument.HasUnsavedChanges())
    {
        GameEngine::Diagnostics::Debug::LogWarning(
            "Save this scene before migrating its references; migration rewrites the scene files "
            "and reloads the open one.");
        return false;
    }
    const EditorAssetMaintenance::SceneMigrationOutcome outcome =
        mAssetMaintenance.ApplySceneMigration(MakeProjectHandles());
    if (!outcome.applied)
    {
        return false;
    }
    // 열려 있던 장면은 방금 파일이 바뀐 그 장면일 수 있다. 메모리의 것은 아직 경로를 쥐고 있어,
    // 다시 읽지 않으면 다음 저장이 이관을 되돌려 쓴다.
    if (outcome.shouldReloadOpenScene && mDocument.HasOpenScene() &&
        !OpenScene(mDocument.GetOpenProjectSceneId()))
    {
        GameEngine::Diagnostics::Debug::LogWarning(
            "The scene files were migrated but the open scene could not be reloaded. Open it "
            "again before saving.");
    }
    return true;
}

std::size_t EditorContext::RestoreSetAsideSidecars()
{
    return mAssetMaintenance.RestoreSetAsideSidecars(MakeProjectHandles());
}

void EditorContext::SurveyOrphanSidecars()
{
    mAssetMaintenance.SurveyOrphanSidecars(MakeProjectHandles());
}

std::string EditorContext::DescribeAssetReference(
    const GameEngine::Assets::AssetReference& reference) const
{
    return mAssetMaintenance.DescribeAssetReference(MakeProjectHandles(), reference);
}

bool EditorContext::ApplyOrphanSidecarCleanup()
{
    return mAssetMaintenance.ApplyOrphanSidecarCleanup(MakeProjectHandles());
}

EditorAssetMaintenance::ProjectHandles EditorContext::MakeProjectHandles() const
{
    return { mOpenProject ? &*mOpenProject : nullptr, mProjectGame.get(), mProjectContent.get(),
        &mGameComponentSchemas };
}

bool EditorContext::RefreshProjectAssets()
{
    if (!mProjectGame || !mProjectContent)
    {
        return false;
    }
    GameEngine::Assets::AssetDatabase refreshed;
    if (!refreshed.Refresh(*mProjectContent))
    {
        // 스캔이 실패했다. 열린 프로젝트의 데이터베이스는 그대로 두고 다음 변동을 기다린다 —
        // 실패한 스캔을 옮기면 장면이 그리는 모든 것이 사라진다.
        return false;
    }
    // 갈아 끼우기 전에 묻는다: 사라진 자리와 나타난 자리 중 같은 것이 있는가. 옛 데이터베이스가
    // 아직 살아 있는 이 순간에만 물을 수 있다. 사이드카를 옮겼다면 방금의 스캔은 그것을 보기
    // 전이므로 다시 읽는다 — 그래야 옮겨 온 에셋이 자기 정체성을 달고 들어온다.
    if (MoveSidecarsForMovedAssets(refreshed) > 0)
    {
        GameEngine::Assets::AssetDatabase remapped;
        if (remapped.Refresh(*mProjectContent))
        {
            refreshed = std::move(remapped);
        }
    }
    // 새 데이터베이스는 정체성과 내용이 같은 에셋의 페이로드를 옛 데이터베이스에서
    // 물려받는다. 파일 하나의 변경 때문에 바뀌지 않은 이미지까지 다시 디코드하지 않는다.
    refreshed.InheritPayloadsFrom(mProjectGame->GetAssetDatabase());
    mProjectGame->GetAssetDatabase() = std::move(refreshed);
    // 정체성을 막 발급했다면 그것은 디스크에만 있고 이 데이터베이스는 아직 모른다. 한 번 더
    // 읽어야 방금 발급된 에셋이 자기 정체성으로 조회되고 패키징도 그것을 본다. 두 번째 쓰기는
    // 없으므로 — 이제 전부 정체성을 갖는다 — 여기서 멈춘다.
    // 두 호출을 한 식에 두지 않는다. + 의 피연산자 평가 순서는 정해져 있지 않아서, 발급이 먼저
    // 돌면 돌아온 파일이 새 정체성을 받아 버린다 — 되돌리기가 지키려던 바로 그것을 잃는다.
    const std::size_t restored = RestoreSetAsideSidecars();
    if (restored + WriteMissingSidecars() > 0)
    {
        GameEngine::Assets::AssetDatabase reread;
        if (reread.Refresh(*mProjectContent))
        {
            mProjectGame->GetAssetDatabase() = std::move(reread);
        }
    }
    // 스캔이 성공한 뒤에만 정리를 묻는다. 실패한 스캔의 데이터베이스는 비어 있어서, 그것으로
    // 물으면 프로젝트의 모든 사이드카가 주인 없는 것으로 보인다.
    SurveyOrphanSidecars();
    ++mProjectRevision;
    return true;
}

GameEngine::Runtime::Scene* EditorContext::GetOpenScene() const
{
    return mDocument.GetOpenScene(MakeDocumentHandles());
}

bool EditorContext::EnterPlayMode()
{
    return mDocument.EnterPlayMode(MakeDocumentHandles());
}

bool EditorContext::ExitPlayMode()
{
    if (!mDocument.ExitPlayMode(MakeDocumentHandles()))
    {
        return false;
    }
    ++mProjectRevision;
    return true;
}

bool EditorContext::SaveOpenScene()
{
    return mDocument.SaveOpenScene(MakeDocumentHandles());
}

std::filesystem::path EditorContext::SaveRecoverySnapshot(
    const std::filesystem::path& recoveryDirectory) const
{
    return mDocument.SaveRecoverySnapshot(MakeDocumentHandles(), recoveryDirectory);
}

bool EditorContext::RestoreRecoverySnapshot(
    const std::filesystem::path& filePath, const unsigned int projectSceneId)
{
    if (!mDocument.RestoreRecoverySnapshot(MakeDocumentHandles(), filePath, projectSceneId))
    {
        return false;
    }
    mSettingsStore.RememberLastScene(projectSceneId);
    ++mProjectRevision;
    return true;
}
}
