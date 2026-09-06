#include "Document/EditorSceneDocument.h"

#include "Rules/EditorFileWrite.h"

#include <utility>
#include <vector>

#include "Assets/AssetDatabase.h"
#include "Platform/TextFile.h"
#include "Diagnostics/Debug.h"
#include "Runtime/Game.h"
#include "Runtime/Object.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Serialization/SceneSerializer.h"

#include "Rules/EditorRecovery.h"

namespace GameEditor
{

void EditorSceneDocument::Close()
{
    // 문서를 닫는 자리는 여기 하나다. 상태가 하나 늘어도 비우는 것을 잊을 자리가 없게 하려는
    // 것이 이 함수의 요점이다.
    mHasOpenScene = false;
    mOpenRuntimeSceneId = 0;
    mSelectedInstanceId = 0;
    mPlay.Clear();
    // 이전 문서가 통째로 사라졌다. 저장하지 못한 편집이 있었더라도 이제 가리킬 것이 없다 —
    // 그것을 막는 일은 이 지점이 아니라 프로젝트를 여는 제스처에서 물어야 한다.
    mHasUnsavedChanges = false;
    // 커맨드들이 참조하던 런타임이 통째로 사라진다.
    mUndo.Reset();
}

GameEngine::Runtime::Object* EditorSceneDocument::FindObject(
    const ProjectHandles& project, const unsigned int instanceId) const
{
    // 별칭 사슬이 먼저다: 커맨드가 잡아 둔 옛 id가 재생성된 객체의 현재 id로 해석되는 지점이
    // 여기 하나다. 별칭이 없는 id는 그대로 지나간다.
    return instanceId == 0 || !project.game
        ? nullptr
        : project.game->FindObject(mUndo.ResolveObjectId(instanceId));
}

void EditorSceneDocument::RecordEdit(std::unique_ptr<IEditCommand> command)
{
    // 기록해도 되는지는 문서의 물음이다 — 열린 장면이 있는 Edit 모드인가. 부품은 그것을 알 수
    // 없고, 답을 쓰는 곳이 여기 하나여야 편집 지점이 늘어도 묻는 것을 잊지 않는다.
    if (!command || !CanRecordEdits())
    {
        return;
    }
    mUndo.Record(std::move(command));
    // 편집이 스택에 남는 순간이 곧 장면이 파일과 달라지는 순간이다.
    MarkEdited();
}

GameEngine::Runtime::Object* EditorSceneDocument::GetSelectedObject(
    const ProjectHandles& project) const
{
    return FindObject(project, mSelectedInstanceId);
}


bool EditorSceneDocument::OpenScene(
    const ProjectHandles& project, const unsigned int projectSceneId)
{
    if (!project.file || !project.content || !project.game)
    {
        GameEngine::Diagnostics::Debug::LogError("Cannot open a scene without an open project.");
        return false;
    }
    const auto scenePath = project.file->settings.scenePaths.find(projectSceneId);
    if (scenePath == project.file->settings.scenePaths.end())
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The project has no scene with this id. sceneId=", projectSceneId);
        return false;
    }

    std::vector<std::byte> sceneBytes;
    if (!project.content->Read(scenePath->second, sceneBytes))
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Failed to read the scene file. path=", scenePath->second.string());
        return false;
    }
    std::unique_ptr<GameEngine::Runtime::Scene> scene =
        GameEngine::Serialization::SceneSerializer::LoadFromBytes(
            sceneBytes, scenePath->second, project.game->GetRuntimeContext());
    if (!scene)
    {
        return false;
    }

    if (!MountScene(project, std::move(scene), projectSceneId, false))
    {
        return false;
    }
    GameEngine::Diagnostics::Debug::Log(
        "Editor opened scene. sceneId=", projectSceneId,
        ", path=", scenePath->second.string());
    return true;
}

bool EditorSceneDocument::MountScene(
    const ProjectHandles& project,
    std::unique_ptr<GameEngine::Runtime::Scene> scene, const unsigned int projectSceneId,
    const bool markUnsaved)
{
    // 하나만 편집한다: 새 장면이 성공적으로 만들어진 뒤에야 이전 것을 내리므로, 읽다 실패한
    // 파일이 편집 중이던 장면까지 잃게 하지는 않는다. 플레이 중이었다면 그 장면과 함께 끝난다.
    if (mHasOpenScene)
    {
        static_cast<void>(project.game->UnloadScene(mOpenRuntimeSceneId));
        mHasOpenScene = false;
    }
    mPlay.Clear();

    const unsigned int runtimeSceneId = project.game->AddScene(std::move(scene));
    if (runtimeSceneId == 0)
    {
        return false;
    }

    mOpenRuntimeSceneId = runtimeSceneId;
    mOpenProjectSceneId = projectSceneId;
    mHasOpenScene = true;
    mSelectedInstanceId = 0;
    // 편집 대상 문서가 바뀌었다. 이전 장면을 겨눈 커맨드들은 여기서 끝난다.
    ResetUndoHistory();
    // 다음 부팅이 이 장면으로 돌아온다.
    // 파일에서 막 읽어 온 것이면 저장할 것이 없다. 복구 사본을 되살린 것이면 그 반대다 —
    // 디스크의 장면 파일과 다른 상태이므로, 저장하지 않으면 다시 잃는다.
    mHasUnsavedChanges = markUnsaved;
    return true;
}

GameEngine::Runtime::Scene* EditorSceneDocument::GetOpenScene(const ProjectHandles& project) const
{
    return mHasOpenScene && project.game
        ? project.game->GetSceneManager().GetScene(mOpenRuntimeSceneId) : nullptr;
}

bool EditorSceneDocument::EnterPlayMode(const ProjectHandles& project)
{
    GameEngine::Runtime::Scene* const scene = GetOpenScene(project);
    if (mPlay.IsPlaying() || !scene)
    {
        return false;
    }
    // 문서는 장면 하나를 편집한다. 추가 활성 장면을 암묵적으로 버리지 않도록 진입에서 확인한다.
    if (project.game->GetSceneManager().GetActiveScenes().size() != 1)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Cannot enter play mode while scenes other than the edited scene are active.");
        return false;
    }
    if (!mPlay.Begin(GameEngine::Serialization::SceneSerializer::SaveToText(*scene)))
    {
        return false;
    }
    // Play 이탈이 장면을 이 스냅숏에서 다시 로드하면 모든 인스턴스 id가 바뀐다. id로 대상을 잡는
    // 커맨드는 그 순간 전부 고아가 되므로, 스택을 유지하는 대신 여기서 비운다. (헤더의 정책 주석
    // 참고.)
    ResetUndoHistory();
    GameEngine::Diagnostics::Debug::Log("Entered play mode. scene=", scene->GetName());
    return true;
}

bool EditorSceneDocument::ExitPlayMode(const ProjectHandles& project)
{
    if (!mPlay.IsPlaying())
    {
        return false;
    }
    // 복원을 끝낼 때까지 Play 표시와 스냅숏을 보존해 실패 후 재시도와 사고 사본 저장을 허용한다.
    const std::string& snapshot = mPlay.GetSnapshot();

    const auto scenePath = project.file
        ? project.file->settings.scenePaths.find(mOpenProjectSceneId)
        : std::unordered_map<unsigned int, std::filesystem::path>::const_iterator{};
    if (!project.file || !project.game || scenePath == project.file->settings.scenePaths.end())
    {
        return false;
    }

    // 플레이 중의 변화를 버리고 들어갈 때의 장면으로 돌아간다. 새 장면이 성공적으로 만들어진
    // 뒤에야 플레이 중이던 것을 내리므로, 스냅숏이 읽히지 않는 사고가 장면 자체를 잃게 하지는
    // 않는다.
    const std::span<const std::byte> bytes = std::as_bytes(std::span(snapshot));
    std::unique_ptr<GameEngine::Runtime::Scene> restored =
        GameEngine::Serialization::SceneSerializer::LoadFromBytes(
            bytes, scenePath->second, project.game->GetRuntimeContext());
    if (!restored)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The scene could not be restored after play mode; the played state is kept.");
        return false;
    }
    // 장면 추가까지 성공한 뒤 실행 장면 집합을 내린다. 원본이 Play 중 언로드되어도 같은 경로다.
    const unsigned int runtimeSceneId = project.game->AddScene(std::move(restored));
    if (runtimeSceneId == 0)
    {
        return false;
    }
    if (!project.game->RetainOnlyScene(runtimeSceneId))
    {
        static_cast<void>(project.game->UnloadScene(runtimeSceneId));
        return false;
    }
    mOpenRuntimeSceneId = runtimeSceneId;
    mHasOpenScene = true;
    static_cast<void>(mPlay.End());
    mSelectedInstanceId = 0;
    // 진입에서 이미 비웠지만, 복원된 장면의 id들이 새것이라는 사실을 여기서도 지킨다.
    ResetUndoHistory();
    GameEngine::Diagnostics::Debug::Log("Exited play mode; the scene was restored.");
    return true;
}

bool EditorSceneDocument::SaveOpenScene(const ProjectHandles& project)
{
    GameEngine::Runtime::Scene* const scene = GetOpenScene(project);
    if (!scene || !project.file)
    {
        GameEngine::Diagnostics::Debug::LogError("There is no open scene to save.");
        return false;
    }
    if (mPlay.IsPlaying())
    {
        // 플레이 중의 장면은 스크립트가 바꾼 상태다. 그것을 파일로 만들면 되돌릴 길이 없다.
        GameEngine::Diagnostics::Debug::LogError("Stop play mode before saving the scene.");
        return false;
    }
    const auto scenePath = project.file->settings.scenePaths.find(mOpenProjectSceneId);
    if (scenePath == project.file->settings.scenePaths.end())
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The open scene has no path in the project settings. sceneId=", mOpenProjectSceneId);
        return false;
    }

    const std::string text = GameEngine::Serialization::SceneSerializer::SaveToText(*scene);
    const std::filesystem::path fullPath =
        project.file->GetAssetRootPath() / scenePath->second;
    if (!WriteFileAtomically(fullPath, text))
    {
        return false;
    }
    // 파일이 방금 장면과 같아졌다.
    mHasUnsavedChanges = false;
    GameEngine::Diagnostics::Debug::Log("Saved the open scene. path=", fullPath.string());
    return true;
}

std::filesystem::path EditorSceneDocument::SaveRecoverySnapshot(
    const ProjectHandles& project, const std::filesystem::path& recoveryDirectory) const
{
    if (!mHasOpenScene || !project.file)
    {
        return {};
    }

    // 플레이 중이면 사람이 편집한 것은 진입할 때 떠 둔 스냅숏이다. 지금 런타임의 상태는
    // 스크립트가 만든 것이라 지킬 작업물이 아니다.
    std::string text = mPlay.GetSnapshot();
    if (!mPlay.IsPlaying())
    {
        const GameEngine::Runtime::Scene* const scene = GetOpenScene(project);
        if (!scene)
        {
            return {};
        }
        text = GameEngine::Serialization::SceneSerializer::SaveToText(*scene);
    }
    if (text.empty())
    {
        return {};
    }

    return WriteRecoveryFile(
        project.file->filePath, mOpenProjectSceneId, text, recoveryDirectory);
}

bool EditorSceneDocument::RestoreRecoverySnapshot(
    const ProjectHandles& project, const std::filesystem::path& filePath,
    const unsigned int projectSceneId)
{
    if (!project.file || !project.game)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Cannot restore a recovery snapshot without an open project.");
        return false;
    }
    if (!RecoveryFileBelongsToProject(filePath, project.file->filePath, projectSceneId))
    {
        GameEngine::Diagnostics::Debug::LogError(
            "This recovery snapshot has no matching project owner; it was left in place. path=",
            filePath.string());
        return false;
    }
    const auto scenePath = project.file->settings.scenePaths.find(projectSceneId);
    if (scenePath == project.file->settings.scenePaths.end())
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The project has no scene with this id, so its recovery snapshot cannot be restored."
            " sceneId=", projectSceneId);
        return false;
    }

    const std::optional<std::string> contents = GameEngine::Platform::ReadTextFile(filePath);
    if (!contents)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Could not read a recovery snapshot. path=", filePath.string());
        return false;
    }
    const std::string& text = *contents;
    if (text.empty())
    {
        GameEngine::Diagnostics::Debug::LogError(
            "A recovery snapshot is empty, so there is nothing to restore. path=",
            filePath.string());
        return false;
    }

    // 사본은 장면 파일 형식 그대로다. 장면의 경로를 함께 넘기는 이유는 그것이 장면의 정체성이고,
    // 되살린 장면이 저장될 자리도 그 경로이기 때문이다.
    const std::span<const std::byte> bytes(
        reinterpret_cast<const std::byte*>(text.data()), text.size());
    std::unique_ptr<GameEngine::Runtime::Scene> scene =
        GameEngine::Serialization::SceneSerializer::LoadFromBytes(
            bytes, scenePath->second, project.game->GetRuntimeContext());
    if (!scene)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "A recovery snapshot could not be read as a scene. path=", filePath.string());
        return false;
    }

    // 되살린 장면은 저장되지 않은 상태로 선다. 디스크의 장면 파일은 사고 이전 그대로이므로,
    // 저장할 것이 없다고 말하면 사람은 되살린 작업을 두 번째로 잃는다.
    if (!MountScene(project, std::move(scene), projectSceneId, true))
    {
        return false;
    }
    GameEngine::Diagnostics::Debug::Log(
        "Restored a recovery snapshot. sceneId=", projectSceneId,
        ", path=", filePath.filename().string());
    return true;
}

}
