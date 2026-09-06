#pragma once

// editor-layer: 1 (Document)

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>

#include "App/ProjectFile.h"
#include "Platform/DirectoryContentSource.h"

#include "Document/EditorPlaySession.h"

#include "Document/EditorUndoService.h"

namespace GameEngine::Runtime
{
class Game;
class Object;
class Scene;
}

namespace GameEditor
{

/// <summary>
/// 지금 편집 중인 문서다: 열려 있는 장면, 그 안에서 고른 것, 저장되지 않은 편집, 되돌리기
/// 이력, 그리고 플레이로 다녀오는 일.
///
/// 이 다섯이 한 부품인 이유는 <b>한 수명을 살기 때문</b>이다. 장면이 닫히면 선택도 이력도
/// 플레이 표시도 가리킬 것이 없다. 그래서 무엇을 비우는지가 아니라 <see cref="Close"/> 하나가
/// 문서를 닫는다 — 상태가 하나 늘어도 비우는 것을 잊을 자리가 없다.
///
/// <b>프로젝트는 상태가 아니라 인자다.</b> 장면은 런타임 안에 살고 그 경로는 프로젝트 설정에
/// 있으므로 셋을 받아야 일할 수 있는데, 프로젝트를 다시 열면 셋이 통째로 바뀐다. 문서가 쥐고
/// 있으면 옛 런타임의 장면 id로 새 런타임을 뒤지게 되므로 호출마다 받는다.
///
/// <b>여기 없는 것 둘</b>: 프로젝트의 리비전과 「다음 부팅이 돌아올 장면」을 설정에 적는 일.
/// 리비전을 올리는 곳은 넷인데 둘이 프로젝트 쪽이고, 설정은 프로젝트 세션의 것이다. 그래서
/// 장면을 올린 뒤 그 둘을 하는 것은 부르는 쪽의 일이며, 이 부품은 <b>올렸는지</b>만 답한다 —
/// 올리지 못한 장면이 다음 부팅의 장면이 되거나 리비전을 올리는 일이 그래서 생기지 않는다.
/// </summary>
class EditorSceneDocument final
{
public:
    /// <summary>
    /// 문서가 일하는 데 필요한 프로젝트다. 셋 다 읽기만 하며, 하나라도 없으면 장면을 열 수 없다.
    /// </summary>
    struct ProjectHandles
    {
        const GameEngine::App::ProjectFileData* file = nullptr;
        GameEngine::Runtime::Game* game = nullptr;
        GameEngine::Platform::DirectoryContentSource* content = nullptr;
    };

    // ---- 열려 있는 장면

    [[nodiscard]] bool HasOpenScene() const { return mHasOpenScene; }
    [[nodiscard]] unsigned int GetOpenProjectSceneId() const { return mOpenProjectSceneId; }

    /// <summary>편집 중인 장면이다. 열린 것이 없으면 null이다.</summary>
    [[nodiscard]] GameEngine::Runtime::Scene* GetOpenScene(const ProjectHandles& project) const;

    /// <summary>프로젝트에 등록된 장면 하나를 읽어 편집 대상으로 세운다.</summary>
    [[nodiscard]] bool OpenScene(const ProjectHandles& project, unsigned int projectSceneId);

    /// <summary>편집 중인 장면을 그 파일에 쓴다.</summary>
    [[nodiscard]] bool SaveOpenScene(const ProjectHandles& project);

    // ---- 고른 것

    /// <summary>
    /// 계층에서 그 오브젝트를 고른 것으로 한다. 고른 에셋이 있었으면 지운다 — 한 번에 하나만
    /// 고를 수 있다. 0은 고른 것이 없다는 뜻이므로, 오브젝트 선택을 지우는 길도 이것 하나다.
    /// </summary>
    void SelectObject(const unsigned int instanceId)
    {
        mSelectedInstanceId = instanceId;
        mSelectedAssetPath.reset();
    }
    [[nodiscard]] unsigned int GetSelectedInstanceId() const { return mSelectedInstanceId; }
    [[nodiscard]] GameEngine::Runtime::Object* GetSelectedObject(
        const ProjectHandles& project) const;

    /// <summary>
    /// 콘텐츠 브라우저에서 고른 에셋의, 프로젝트 상대 경로다. 고른 것이 없으면 값이 없다.
    /// </summary>
    [[nodiscard]] std::optional<std::filesystem::path> GetSelectedAssetPath() const
    {
        return mSelectedAssetPath;
    }
    /// <summary>
    /// 콘텐츠 브라우저에서 그 에셋을 고른 것으로 한다. 고른 오브젝트가 있었으면 지운다 — 한
    /// 번에 하나만 고를 수 있다.
    /// </summary>
    void SelectAsset(std::filesystem::path relativePath)
    {
        mSelectedInstanceId = 0;
        mSelectedAssetPath = std::move(relativePath);
    }
    void ClearAssetSelection() { mSelectedAssetPath.reset(); }

    [[nodiscard]] std::optional<unsigned int> GetSelectedSceneId() const
    {
        return mSelectedSceneId;
    }
    void SelectScene(const unsigned int projectSceneId) { mSelectedSceneId = projectSceneId; }
    void ClearSceneSelection() { mSelectedSceneId.reset(); }

    // ---- 저장되지 않은 편집

    [[nodiscard]] bool HasUnsavedChanges() const { return mHasUnsavedChanges; }
    void MarkEdited() { mHasUnsavedChanges = true; }

    // ---- 되돌리기

    [[nodiscard]] UndoStack& GetUndoStack() { return mUndo.GetStack(); }
    [[nodiscard]] unsigned int ResolveObjectId(const unsigned int instanceId) const
    {
        return mUndo.ResolveObjectId(instanceId);
    }
    void RecordObjectIdAlias(const unsigned int oldId, const unsigned int newId)
    {
        mUndo.RecordObjectIdAlias(oldId, newId);
    }
    void ResetUndoHistory() { mUndo.Reset(); }
    void RecordEdit(std::unique_ptr<IEditCommand> command);

    /// <summary>별칭으로 해석한 뒤 런타임에서 찾는다. 그 둘이 한 걸음이어야 하는 자리다.</summary>
    [[nodiscard]] GameEngine::Runtime::Object* FindObject(
        const ProjectHandles& project, unsigned int instanceId) const;

    // ---- 플레이

    [[nodiscard]] bool IsPlaying() const { return mPlay.IsPlaying(); }
    [[nodiscard]] bool IsPlayInputCaptured() const { return mPlay.IsInputCaptured(); }
    void SetPlayInputCaptured(const bool captured) { mPlay.SetInputCaptured(captured); }
    /// <summary>편집 장면 하나만 활성인 상태에서 Play를 시작한다. 추가 활성 장면이 있으면 거절한다.</summary>
    [[nodiscard]] bool EnterPlayMode(const ProjectHandles& project);
    /// <summary>
    /// 실행 중의 활성 장면을 모두 내리고 진입 시점의 편집 장면으로 복원한다.
    /// 복원 준비가 실패하면 Play 상태와 스냅숏을 유지해 재시도와 사고 사본 저장을 허용한다.
    /// </summary>
    [[nodiscard]] bool ExitPlayMode(const ProjectHandles& project);

    /// <summary>
    /// 문서를 닫는다. 열린 장면, 고른 것, 저장되지 않은 편집, 되돌리기 이력, 플레이 표시가
    /// 한 번에 사라진다 — 문서가 한 수명이므로 비우는 자리도 하나여야 한다.
    ///
    /// 계층에서 골라 둔 장면(<see cref="GetSelectedSceneId"/>), 콘텐츠 브라우저에서 고른
    /// 에셋(<see cref="GetSelectedAssetPath"/>), 마지막으로 연 장면의 프로젝트 id는 여기서
    /// 지우지 않는다. 지금 동작이 그러하며, 바꾸는 것은 이 단위의 밖이다.
    /// </summary>
    void Close();

    // ---- 사고 사본

    [[nodiscard]] std::filesystem::path SaveRecoverySnapshot(
        const ProjectHandles& project, const std::filesystem::path& recoveryDirectory) const;
    [[nodiscard]] bool RestoreRecoverySnapshot(
        const ProjectHandles& project, const std::filesystem::path& filePath,
        unsigned int projectSceneId);

private:
    /// <summary>
    /// 만들어진 장면을 편집 대상으로 세운다. 여는 것과 되살리는 것이 같은 걸음을 밟게 하려고
    /// 하나로 둔다. 설정과 리비전은 여기서 건드리지 않는다 — 부르는 쪽의 일이다.
    /// </summary>
    [[nodiscard]] bool MountScene(
        const ProjectHandles& project, std::unique_ptr<GameEngine::Runtime::Scene> scene,
        unsigned int projectSceneId, bool markUnsaved);

    /// <summary>
    /// 지금의 편집이 undo로 기록될 수 있는지다: 열린 장면이 있는 Edit 모드다.
    ///
    /// 비공개인 것이 이 규칙의 요점이다. 공개 질의라면 편집 지점 열 곳이 각자 묻고 각자
    /// 기록하게 되고, 그중 하나가 묻는 것을 잊는 데는 새 편집 경로 하나면 충분하다. 답을 쓰는
    /// 곳은 <see cref="RecordEdit"/> 하나뿐이다.
    /// </summary>
    [[nodiscard]] bool CanRecordEdits() const { return mHasOpenScene && !mPlay.IsPlaying(); }

    /// <summary>편집 중인 장면이 런타임 안에서 사는 id이다. Game::AddScene이 발급했다.</summary>
    unsigned int mOpenRuntimeSceneId = 0;
    /// <summary>그 장면이 프로젝트 설정에서 갖는 id이다.</summary>
    unsigned int mOpenProjectSceneId = 0;
    bool mHasOpenScene = false;
    /// <summary>계층에서 고른 오브젝트다. 0이면 고른 것이 없다.</summary>
    unsigned int mSelectedInstanceId = 0;
    /// <summary>
    /// 콘텐츠 브라우저에서 고른 에셋이다. <see cref="mSelectedSceneId"/>처럼 <see cref="Close"/>가
    /// 지우지 않는다 — 에셋은 열린 장면이 아니라 프로젝트에 속하므로, 장면을 닫아도 여전히
    /// 뜻이 있다.
    /// </summary>
    std::optional<std::filesystem::path> mSelectedAssetPath;
    /// <summary>계층에서 골라 둔 장면이다. 여는 것과 별개다.</summary>
    std::optional<unsigned int> mSelectedSceneId;
    /// <summary>저장 이후 편집이 있었는지다. 보수적으로 참에 머문다.</summary>
    bool mHasUnsavedChanges = false;

    /// <summary>플레이 표시와 입력 포획, 진입 시점의 장면이다.</summary>
    EditorPlaySession mPlay;
    /// <summary>되돌리기 이력과 객체 id 별칭이다.</summary>
    EditorUndoService mUndo;
};

}
