#pragma once

// editor-layer: 1 (Document)

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "App/EditorSettings.h"
#include "Document/EditorSettingsStore.h"
#include "App/ProjectFile.h"
#include "Core/Guid.h"
#include "Core/UndoStack.h"
#include "Serialization/ComponentSchema.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IDirectoryWatcher.h"
#include "Document/EditorAssetMaintenance.h"
#include "Rules/EditorObjectHost.h"
#include "Document/EditorSceneDocument.h"
#include "Document/EditorProjectWatch.h"
#include "Rules/OrphanSidecars.h"
#include "Rules/SceneReferenceMigration.h"

namespace GameEngine::Assets
{
class AssetDatabase;
}

namespace GameEngine::Runtime
{
class Game;
class Object;
class Scene;
}
namespace GameEditor
{

/// <summary>
/// 에디터의 문서 모델이다: 열린 프로젝트, 편집 중인 장면, 선택. UI가 어떻게 그려지는지는
/// 모른다 — 그래서 UI가 Win32 컨트롤에서 엔진 렌더링으로 바뀌어도 이 클래스는 그대로다.
///
/// 편집 대상 프로젝트는 자기만의 런타임(<see cref="GameEngine::Runtime::Game"/>)에 산다.
/// 에디터 프로세스를 부팅한 런타임과는 별개라서, 에디터가 자기 자신의 프로젝트를 열어도 장면이
/// 두 번 보이지 않고, 프로젝트를 바꾸면 런타임을 통째로 새로 세운다 — 이전 프로젝트의 잔여가
/// 남을 자리가 없다.
/// </summary>
class EditorContext final : public IEditorObjectHost
{
public:
    /// <summary>실행 파일 옆의 에디터 콘텐츠와 기본 설정 파일을 사용한다.</summary>
    EditorContext();
    /// <summary>
    /// 에디터 자체 콘텐츠와 설정 파일의 위치를 명시한다. 편집 대상 프로젝트는 OpenProject가
    /// 별도로 고르며, 테스트는 이 두 파일 의존성도 자기 임시 디렉터리에 격리할 수 있다.
    /// </summary>
    EditorContext(const std::filesystem::path& editorContentRoot,
        std::filesystem::path settingsFilePath);
    ~EditorContext();

    /// <summary>편집 대상 프로젝트의 런타임이다. 열린 프로젝트가 없으면 null이다.</summary>
    [[nodiscard]] GameEngine::Runtime::Game* GetProjectGame() const { return mProjectGame.get(); }

    /// <summary>편집 대상 런타임에서 객체를 찾는다. 프로젝트가 없으면 null이다.</summary>
    [[nodiscard]] GameEngine::Runtime::Object* FindObject(unsigned int instanceId) const;
    [[nodiscard]] GameEngine::Runtime::Component* FindComponent(unsigned int instanceId) override;
    [[nodiscard]] GameEngine::Runtime::GameObject* FindGameObject(unsigned int instanceId) override;

    void SelectObject(const unsigned int instanceId) override { mDocument.SelectObject(instanceId); }
    [[nodiscard]] unsigned int GetSelectedInstanceId() const
    {
        return mDocument.GetSelectedInstanceId();
    }
    [[nodiscard]] GameEngine::Runtime::Object* GetSelectedObject() const;

    /// <summary>
    /// 콘텐츠 브라우저에서 고른 에셋의, 프로젝트 상대 경로다. 고른 것이 없으면 값이 없다.
    /// </summary>
    [[nodiscard]] std::optional<std::filesystem::path> GetSelectedAssetPath() const
    {
        return mDocument.GetSelectedAssetPath();
    }
    /// <summary>
    /// 콘텐츠 브라우저에서 그 에셋을 고른 것으로 한다. 고른 오브젝트가 있었으면 지운다 — 선택은
    /// 한 번에 하나뿐이다.
    /// </summary>
    void SelectAsset(std::filesystem::path relativePath)
    {
        mDocument.SelectAsset(std::move(relativePath));
    }
    void ClearAssetSelection() { mDocument.ClearAssetSelection(); }

    [[nodiscard]] bool OpenProject(const std::filesystem::path& projectFilePath);

    /// <summary>
    /// 열린 프로젝트의 장면 하나를 에디터 런타임에 로드한다. 이전에 열려 있던 프로젝트 장면은
    /// 먼저 내린다 — 에디터는 한 번에 하나의 장면을 편집한다.
    /// </summary>
    /// <param name="projectSceneId">프로젝트 설정의 scenePaths에 적힌 장면 id이다.</param>
    [[nodiscard]] bool OpenScene(unsigned int projectSceneId);

    /// <summary>지금 편집 중인 프로젝트 장면이다. 열린 것이 없으면 null이다.</summary>
    [[nodiscard]] GameEngine::Runtime::Scene* GetOpenScene() const override;

    /// <summary>편집 중인 장면이 온 프로젝트 설정상의 장면 id이다. 열린 것이 없으면 의미 없다.</summary>
    [[nodiscard]] unsigned int GetOpenProjectSceneId() const
    {
        return mDocument.GetOpenProjectSceneId();
    }
    [[nodiscard]] bool HasOpenScene() const { return mDocument.HasOpenScene(); }

    /// <summary>
    /// 계층에서 골라 둔 장면 파일이다. 고른 것이 없으면 값이 없다.
    ///
    /// 여는 것과 고르는 것이 다른 이유는, 이름을 바꾸거나 지울 대상이 <b>열지 않은 장면</b>일 수
    /// 있기 때문이다. 지우려고 먼저 열어야 한다면, 지우는 순간 편집 중인 장면이 사라진다.
    /// </summary>
    [[nodiscard]] std::optional<unsigned int> GetSelectedSceneId() const
    {
        return mDocument.GetSelectedSceneId();
    }
    void SelectScene(const unsigned int projectSceneId) { mDocument.SelectScene(projectSceneId); }
    void ClearSceneSelection() { mDocument.ClearSceneSelection(); }

    /// <summary>
    /// 편집 중인 장면을 프로젝트의 장면 파일에 다시 쓴다. 형식은 로더가 읽는 그대로라서, 저장한
    /// 장면은 플레이어와 에디터 어느 쪽에서도 다시 열린다.
    /// </summary>
    [[nodiscard]] bool SaveOpenScene();

    /// <summary>
    /// 저장한 뒤로 편집이 있었는지다. 참이면 편집 중인 장면이 파일과 다르다.
    ///
    /// 장면이나 프로젝트를 떠나기 전에 저장되지 않은 편집을 표시하고 확인하는 데 쓴다.
    ///
    /// 보수적으로 답한다. 편집을 전부 되돌려 파일과 같은 상태로 돌아와도 참으로 남는데, undo
    /// 스택의 깊이로는 그것을 정확히 셀 수 없기 때문이다: 용량을 넘겨 버려진 편집과 TryMerge로
    /// 흡수된 편집은 깊이를 늘리지 않으므로, 깊이가 같아도 문서가 다를 수 있다. 틀린다면
    /// "저장했는데 안 했다고 말하는" 쪽이 그 반대보다 훨씬 싸다.
    ///
    /// 플레이는 이 답을 바꾸지 않는다. 플레이 이탈은 진입할 때 뜬 스냅숏 — 저장되지 않은 편집을
    /// 담은 그 상태 — 으로 되돌리므로, 플레이를 다녀와도 파일과의 차이는 그대로다.
    /// </summary>
    [[nodiscard]] bool HasUnsavedChanges() const { return mDocument.HasUnsavedChanges(); }

    /// <summary>
    /// 편집이 일어났음을 알린다. <see cref="RecordEdit"/>이 스스로 부르고, 스택을 직접 움직이는
    /// undo/redo도 부른다 — 그 둘도 장면을 파일과 다르게 만든다.
    /// </summary>
    void MarkEdited() { mDocument.MarkEdited(); }

    /// <summary>
    /// 편집 중인 장면을 임시 디렉터리의 복구 파일에 쓴다. 실행이 비정상적으로 끝나기 직전에만
    /// 불린다 — 장치 손실 같은 프레임 실패는 곧 프로세스 종료이고, 그때까지의 편집은 저장하지
    /// 않았다면 그대로 사라진다.
    ///
    /// 프로젝트의 장면 파일 위에 쓰지 않는 이유는, 사용자가 저장을 누른 적이 없기 때문이다.
    /// 사고가 사용자를 대신해 저장을 결정해서는 안 된다. 임시 파일은 로그가 경로를 말하므로
    /// 원하면 가져다 쓰고, 원치 않으면 무시하면 된다.
    ///
    /// 플레이 중이면 들어갈 때 떠 둔 스냅숏을 쓴다. 그것이 사람이 편집한 장면이고, 지금 런타임의
    /// 상태는 스크립트가 만든 것이라 지킬 작업물이 아니다.
    /// </summary>
    /// <returns>기록한 파일 경로이며, 쓸 것이 없거나 실패하면 비어 있다.</returns>
    /// <param name="recoveryDirectory">사본을 둘 디렉터리다. 사람이 쓰는 자리는
    /// <c>GetRecoveryDirectory()</c>이고, 시험은 자기 프로세스의 임시 자리를 준다.</param>
    [[nodiscard]] std::filesystem::path SaveRecoverySnapshot(
        const std::filesystem::path& recoveryDirectory) const;

    /// <summary>
    /// 복구 사본 하나를 지금 열린 프로젝트의 그 장면으로 되살린다. 되살린 장면은 저장되지
    /// 않은 상태로 선다 — 디스크의 장면 파일은 사고 이전 그대로이므로, 저장할 것이 없다고
    /// 말하면 사람은 되살린 작업을 두 번째로 잃는다.
    ///
    /// 사본을 지우지는 않는다. 지우는 것은 사람이 "버린다"고 답했을 때만이다.
    /// </summary>
    /// <param name="filePath">복구 파일 경로다.</param>
    /// <param name="projectSceneId">되살릴 장면의 프로젝트 안 id다.</param>
    /// <returns>되살렸으면 true다.</returns>
    [[nodiscard]] bool RestoreRecoverySnapshot(
        const std::filesystem::path& filePath, unsigned int projectSceneId);

    // ---- 편집 / 플레이
    //
    // 편집 모드에서는 프로젝트 런타임이 멈춰 있다: 스크립트가 돌지 않으므로 사람이 놓은 값이
    // 그대로 남는다. 플레이에 들어가면 열린 장면을 스냅숏으로 떠 두고 런타임을 돌리며, 나오면
    // 그 스냅숏을 다시 로드해 플레이 중의 변화를 버린다 — Unity와 같은 규칙이다. 스냅숏은
    // 장면 파일 형식 그대로라서, 플레이되는 것이 곧 저장될 것이다.

    [[nodiscard]] bool IsPlaying() const { return mDocument.IsPlaying(); }

    /// <summary>
    /// Play 중 게임이 이번 프레임의 입력을 쥐고 있는지다. 게임 뷰를 클릭하면 참이 되고 Esc로
    /// 거짓이 된다. 문서 모델이 이것을 쥐는 이유는 두 쪽이 같은 답을 보아야 하기 때문이다 —
    /// 입력을 나누는 부트스트랩과, 잡혔음을 테두리로 보이는 게임 뷰.
    /// </summary>
    [[nodiscard]] bool IsPlayInputCaptured() const { return mDocument.IsPlayInputCaptured(); }

    /// <summary>잡기 상태를 이번 프레임의 판정으로 맞춘다.</summary>
    /// <param name="captured">게임이 쥐고 있으면 true다.</param>
    void SetPlayInputCaptured(const bool captured)
    {
        mDocument.SetPlayInputCaptured(captured);
    }
    /// <summary>열린 장면으로 플레이를 시작한다. 장면이 없으면 false다.</summary>
    [[nodiscard]] bool EnterPlayMode();
    /// <summary>플레이를 끝내고 들어갈 때의 장면을 되돌린다.</summary>
    [[nodiscard]] bool ExitPlayMode();

    // ---- 장면 참조의 정체성 이관
    //
    // 장면은 에셋을 경로로 가리키다가 정체성으로 가리키게 된다. 이 전환만 사람에게 묻는 이유는
    // 되돌리기 어려운 유일한 단계이기 때문이다 — .meta를 만들고 옮기는 것은 파일 하나가 늘거나
    // 자리를 옮길 뿐이지만, 이관은 사람이 만든 장면 파일을 다시 쓴다.

    /// <summary>
    /// 사람에게 물을 이관 계획이다. 물을 것이 없으면 null이며, 답을 받으면 사라진다.
    /// </summary>
    [[nodiscard]] const SceneMigrationPlan* GetSceneMigrationPlan() const
    {
        return mAssetMaintenance.GetSceneMigrationPlan();
    }

    /// <summary>
    /// 계획대로 장면 파일들을 다시 쓴다. 확인 줄의 진행 버튼이 부른다.
    ///
    /// 모두 성공할 수 있을 때에만 하나라도 쓴다: 먼저 전부를 메모리에서 바꿔 보고, 하나라도
    /// 실패하면 파일은 하나도 건드리지 않는다. 절반만 이관된 프로젝트는 한 장면에 두 형식이
    /// 섞인 상태와 같은데, 그것이 이 관문이 막으려는 바로 그 상태다.
    ///
    /// 쓰기 직전 장면마다 <c>.bak-&lt;시각&gt;</c> 사본을 남긴다.
    /// </summary>
    /// <returns>계획한 장면이 모두 다시 쓰였으면 true다.</returns>
    [[nodiscard]] bool ApplySceneMigration();

    /// <summary>
    /// 이번에는 하지 않는다. 아무것도 기록하지 않으므로 다음에 열 때 다시 묻는다 — 영구 거부는
    /// 「경로로 계속 쓰겠다」는 설정이고, 그것은 이것과 다른 결정이다.
    /// </summary>
    void DismissSceneMigration() { mAssetMaintenance.DismissSceneMigration(); }

    /// <summary>
    /// 이 프로젝트의 장면들이 이미 정체성으로 가리키고 있는지다. 새로 만드는 참조를 어느 형식으로
    /// 쓸지가 이 답에 달렸다 — 한 장면에 두 형식이 섞이지 않게 하는 것이 목적이다.
    ///
    /// 파일에 적어 두지 않고 장면에서 읽는 이유는 두 가지다. .gameproject의 직렬화는 자기가 아는
    /// 열쇠만 적으므로 모르는 열쇠는 장면 하나를 더하는 순간 조용히 사라지고, 무엇보다 이 값은
    /// 장면 자신이 이미 답하고 있는 것이라 따로 적으면 어긋날 수 있는 두 번째 사본이 된다.
    /// </summary>
    [[nodiscard]] bool AreSceneReferencesMigrated() const
    {
        return mAssetMaintenance.AreSceneReferencesMigrated();
    }

    // ---- 주인 없는 사이드카
    //
    // 에셋을 지우면 그 곁의 .meta가 남는다. 쌓이면 프로젝트가 지저분해지지만, 지우는 일은
    // 되돌릴 수 없으므로 두 단계로 나뉜다: 먼저 이름만 바꿔 옆으로 치우고, 다음에 그것이
    // 여전히 주인 없이 남아 있을 때에만 지운다. 그리고 장면이 그 정체성을 가리키고 있으면
    // 어느 단계도 손대지 않는다 — 그것은 고아가 아니라 파일이 없는 에셋이다.

    /// <summary>사람에게 물을 사이드카 정리 계획이다. 물을 것이 없으면 null이다.</summary>
    [[nodiscard]] const OrphanSidecarPlan* GetOrphanSidecarPlan() const
    {
        return mAssetMaintenance.GetOrphanSidecarPlan();
    }

    /// <summary>
    /// 계획이 이번에 묻고 있는 한 단계를 실행한다. 확인 줄의 진행 버튼이 부른다.
    ///
    /// 지울 것이 있으면 지우고, 없으면 치운다 — 계획이 묻는 것과 하는 것이 언제나 같다.
    /// </summary>
    /// <returns>계획한 파일이 모두 처리됐으면 true다.</returns>
    [[nodiscard]] bool ApplyOrphanSidecarCleanup();

    /// <summary>이번에는 하지 않는다. 아무것도 적지 않으므로 다음 스캔에서 다시 묻는다.</summary>
    void DismissOrphanSidecarCleanup() { mAssetMaintenance.DismissOrphanSidecarCleanup(); }

    /// <summary>
    /// 이 참조를 인스펙터가 보일 글자로 옮긴다.
    ///
    /// 엔진의 <see cref="GameEngine::Assets::DescribeAssetReference"/>가 답하지 못하는,
    /// 파일은 사라졌지만 사이드카는 남아 있는 참조를 먼저 처리한다. 정체성의 32자리
    /// 16진수 대신 남은 사이드카 이름에서 얻은 파일 경로를 보여 준다.
    ///
    /// 이 앎이 데이터베이스가 아니라 에디터에 있는 이유는, 그것이 등록된 에셋에 대한 사실이
    /// 아니라 <b>등록되지 않은 파일에 대한 추측</b>이기 때문이다. 런타임은 없는 에셋을 그리지
    /// 않으므로 이 문장이 필요 없고, 필요한 것은 무엇을 고쳐야 하는지 보는 사람뿐이다.
    /// </summary>
    [[nodiscard]] std::string DescribeAssetReference(
        const GameEngine::Assets::AssetReference& reference) const;

    [[nodiscard]] bool HasOpenProject() const { return mOpenProject.has_value(); }
    [[nodiscard]] const GameEngine::App::ProjectFileData* GetOpenProject() const
    {
        return mOpenProject ? &*mOpenProject : nullptr;
    }
    /// <summary>
    /// 열린 프로젝트의 에셋 데이터베이스이다. 열린 것이 없으면 null이다.
    ///
    /// 프로젝트를 읽고 해시하는 비용이 중복되지 않도록 런타임의 데이터베이스를 공유한다.
    /// </summary>
    [[nodiscard]] const GameEngine::Assets::AssetDatabase* GetProjectAssetDatabase() const;

    /// <summary>
    /// 에디터 자신의 에셋 — 창 프레임, 폰트 — 을 읽는 데이터베이스이다.
    ///
    /// 런타임의 데이터베이스와 분리된 이유는, 프로젝트를 열면 런타임이 그 프로젝트를 가리키게
    /// 되기 때문이다: 그 순간부터 런타임 데이터베이스에는 에디터의 파일이 없고, 에디터 UI가
    /// 자기 스프라이트를 잃는다.
    /// </summary>
    [[nodiscard]] const GameEngine::Assets::AssetDatabase* GetEditorAssetDatabase() const
    {
        return mEditorAssetDatabase.get();
    }
    [[nodiscard]] unsigned int GetProjectRevision() const { return mProjectRevision; }
    /// <summary>
    /// 프로젝트 디렉터리의 변동을 듣고, 잠잠해졌으면 에셋 데이터베이스를 다시 읽는다. 프레임마다
    /// 한 번 불린다. 기다리지 않으므로 변동이 없으면 거의 아무 일도 하지 않는다.
    /// </summary>
    void PollProjectAssetChanges();

    /// <summary>
    /// 열린 프로젝트를 다시 스캔해 에셋 데이터베이스를 갈아 끼운다. 성공하면 프로젝트 개정이
    /// 오르고 브라우저와 인스펙터가 새 목록을 읽는다.
    ///
    /// 새 데이터베이스에 스캔한 뒤 성공한 것만 옮기는 이유는 <see cref="Refresh"/>가 실패하면
    /// 자기를 비우기 때문이다. 열린 프로젝트의 것을 그 자리에서 스캔하면, 복사 도중 잠긴 파일
    /// 하나가 편집 중인 장면이 그리는 모든 에셋을 사라지게 한다.
    /// </summary>
    /// <returns>다시 읽어 갈아 끼웠으면 true다.</returns>
    bool RefreshProjectAssets();

    /// <summary>
    /// 사이드카가 없는 에셋마다 기본 사이드카를 디스크에 놓는다. 이미 있는 파일은 — 옛 이름의
    /// 것도 — 절대 덮지 않는다. 그 파일들이 사람이 정한 설정이기 때문이다.
    ///
    /// 여기가 그 자리인 이유는 콘텐츠 소스가 읽기 전용이고 런타임이 프로젝트에 쓰지 않기
    /// 때문이다. 에셋은 내용을 값으로 답하고, 그것을 파일로 만드는 것은 에디터다.
    /// </summary>
    /// <returns>새로 만든 파일 수다.</returns>
    std::size_t WriteMissingSidecars();

    /// <summary>
    /// 갓 스캔한 목록을 지금 열려 있는 것과 견주어, 옮겨지거나 이름이 바뀐 에셋의 사이드카를
    /// 따라 옮긴다. 사람이 탐색기에서 파일만 끌어 옮기면 정체성은 옛 자리에 남는데, 그대로 두면
    /// 그 에셋은 새 정체성을 발급받고 그것을 가리키던 참조는 전부 끊긴다.
    ///
    /// 갈아 끼우기 전에 불러야 한다. 옛 데이터베이스가 무엇이 어디에 있었는지 아는 유일한 곳이고,
    /// 그것을 <see cref="RefreshProjectAssets"/>가 곧 버리기 때문이다. 발급보다 앞이라는 조건도
    /// 같은 자리에서 저절로 따라온다.
    ///
    /// 무엇을 이동으로 볼지는 <see cref="GameEngine::Assets::MatchAssetMoves"/>가 정한다. 여기는
    /// 그 판정에 필요한 두 목록을 만들고, 판정된 것을 파일로 옮기고, 옮기지 않은 것은 왜 옮기지
    /// 않았는지 로그에 남기는 일만 한다.
    /// </summary>
    /// <param name="rescanned">방금 스캔했고 아직 갈아 끼우지 않은 데이터베이스다.</param>
    /// <returns>실제로 옮긴 사이드카 수다.</returns>
    std::size_t MoveSidecarsForMovedAssets(const GameEngine::Assets::AssetDatabase& rescanned);
    /// <summary>
    /// 프로젝트의 장면들을 훑어 이관 계획을 세운다. 프로젝트를 열 때, 정체성 발급이 끝난 뒤에
    /// 부른다 — 그 전에 물으면 아직 정체성이 없는 에셋 때문에 늘 막힌다.
    /// </summary>
    void SurveySceneMigration();

    /// <summary>
    /// 주인 없는 사이드카를 훑어 정리 계획을 세운다. 성공한 스캔 뒤에만 부른다 — 실패한 스캔의
    /// 데이터베이스는 비어 있어서, 그것으로 물으면 프로젝트의 모든 사이드카가 고아로 보인다.
    /// </summary>
    void SurveyOrphanSidecars();

    /// <summary>
    /// 옆으로 치워 둔 사이드카의 주인이 돌아왔으면 이름을 되돌린다.
    ///
    /// 정체성 발급보다 먼저 불러야 한다. 늦으면 돌아온 파일이 새 정체성을 발급받고, 되돌려 놓을
    /// 옛 정체성은 갈 곳을 잃는다 — 치우는 일이 정체성을 잃게 만드는 셈이 되어, 애초에 치우지
    /// 않느니만 못하다.
    /// </summary>
    /// <returns>이름을 되돌린 파일 수다.</returns>
    std::size_t RestoreSetAsideSidecars();

    /// <summary>
    /// 장면 파일 하나를 편집 대상과 무관한 새 Scene으로 읽는다. 훑기와 이관이 열린 장면을
    /// 건드리지 않고 모든 장면을 볼 수 있는 길이다.
    /// </summary>
    /// <returns>읽은 장면이며, 읽지 못하면 null이다.</returns>
    [[nodiscard]] EditorAssetMaintenance::ProjectHandles MakeProjectHandles() const;

    [[nodiscard]] std::unique_ptr<GameEngine::Runtime::Scene> LoadSceneForMigration(
        const std::filesystem::path& relativeScenePath) const;


    // ---- undo/redo
    //
    // 스택은 Edit 모드의 편집만 담는다. 커맨드는 대상을 인스턴스 id로 잡는데, 열린 장면이 바뀌는
    // 모든 전환 — 프로젝트/장면 열기, Play 진입/이탈 — 은 객체들을 새 id로 다시 세우므로 그때마다
    // 스택을 비운다. 특히 Play는 "유지하되 기록만 중단"이 이상적이지만, Play 이탈이 장면을 직렬화
    // 스냅숏에서 다시 로드해 모든 id가 바뀌기 때문에, id로 대상을 잡는 커맨드는 전부 고아가 된다
    // — 그래서 유지 대신 진입 시점에 비우는 것이 정직한 정책이다.
    //
    // 커맨드가 잡은 id와 살아 있는 객체 사이에는 별칭 맵이 있다: 삭제의 undo처럼 객체를
    // 재생성하는 커맨드는 재생성된 객체가 받은 새 id를 옛 id의 별칭으로 등록하고, FindObject가
    // 해석 전에 별칭 사슬을 따라간다. 그래서 커맨드는 처음 잡은 id를 영원히 쥐고 있으면 되고,
    // 스택의 커맨드들을 돌며 id를 고쳐 쓰는 브로드캐스트는 없다. 별칭 맵의 수명은 스택과 같다.

    /// <summary>에디터 편집의 undo/redo 스택이다. 셸이 기록하고 실행한다.</summary>
    [[nodiscard]] GameEngine::Core::UndoStack& GetUndoStack() { return mDocument.GetUndoStack(); }

    /// <summary>
    /// 이미 적용된 편집 하나를 undo 스택에 기록한다.
    ///
    /// "편집은 하되 기록만 건너뛴다"는 규칙이 사는 유일한 자리다: 기록할 수 없는 상태 — 열린
    /// 장면이 없거나 Play 중 — 이면 커맨드를 조용히 버린다. 이 규칙이 호출 지점마다 손으로
    /// 복사돼 있으면 언젠가 한 곳이 어긋나고, 어긋난 곳에서는 Play 중에 칠한 획이 편집 모드의
    /// 스택에 쌓인다.
    ///
    /// 실행은 하지 않는다. 편집은 이미 각자의 길 — 인스펙터의 setter, 계층 창의 드래그,
    /// 커맨드 자신의 Apply — 로 일어난 뒤이고, 스택은 그 사실을 기록할 뿐이다.
    /// </summary>
    /// <param name="command">기록할 커맨드다. 기록하지 않기로 하면 여기서 사라진다.</param>
    void RecordEdit(std::unique_ptr<GameEngine::Core::IEditCommand> command);

    /// <summary>
    /// 재생성으로 id가 바뀐 객체의 별칭을 등록한다: 이후 oldId의 해석은 newId의 살아 있는
    /// 객체에 닿는다. oldId에 이미 별칭 사슬이 있으면 그 끝에 잇는다 — oldId를 잡은 커맨드도,
    /// 그 사슬 중간의 id를 잡은 커맨드도 같은 곳에 닿는다.
    /// </summary>
    void RecordObjectIdAlias(unsigned int oldId, unsigned int newId) override;

    /// <summary>undo 역사를 통째로 버린다: 스택과 별칭 맵. 문서가 바뀌는 전환들이 부른다.</summary>
    void ResetUndoHistory();

    // ---- 게임 프로젝트가 정의한 컴포넌트
    //
    // 이 프로세스에 등록되지 않은 게임 컴포넌트는 장면의 JSON을 보존 데이터로 읽는다.
    // 게임 빌드의 스키마가 있으면 속성 행과 기본값을 알 수 있어 인스펙터 편집과 추가가 가능하다.
    // 스키마는 데이터 모양만 전달한다. 등록된 실행 코드가 없는 컴포넌트는 Play 중 실행되지 않는다.

    /// <summary>열린 프로젝트가 정의한 컴포넌트 타입들의 스키마다. 없으면 비어 있다.</summary>
    [[nodiscard]] const std::vector<GameEngine::Serialization::ComponentSchema>&
        GetGameComponentSchemas() const
    {
        return mGameComponentSchemas;
    }

    /// <summary>이 타입 이름의 스키마다. 스키마가 없거나 그 타입이 없으면 nullptr이다.</summary>
    [[nodiscard]] const GameEngine::Serialization::ComponentSchema* FindGameComponentSchema(
        std::string_view typeName) const;

    // ---- 에디터 설정 (사용자·머신 상태)
    //
    // 설정 파일은 실행 파일 옆의 GameEditor.settings.json이다. 이 저장소의 배포 철학은 실행
    // 파일이 자기 파일을 자기 옆에서 찾는 것이고 — 에디터의 에셋도, 플레이어의 프로젝트도
    // 그렇다 — 에디터는 개발 도구라 자기 디렉터리에 쓸 수 있다. 사용자 디렉터리는 그 철학에
    // 없는 두 번째 탐색 규칙을 만들었을 것이다.
    //
    // 저장은 상태가 바뀌는 사건의 시점이다: 패널 스왑, 프로젝트/장면 열기, 카메라 제스처 종료.
    // 즉시 모드 UI는 매 프레임 값을 다시 말하므로 프레임이 아니라 사건에 걸어야 디스크가
    // 조용하고, 내용이 같은 저장은 건너뛴다. 읽기는 실패해도 조용히 기본값이다 — 첫 실행과
    // 손상된 파일은 오류가 아니라 기본 상태다.

    /// <summary>
    /// 에디터 설정 파일의 경로다. 이름과 자리가 여기 한 번만 적히도록, 컨텍스트가 세워지기 전에
    /// 설정을 읽어야 하는 부팅 경로도 이것을 쓴다.
    /// </summary>
    [[nodiscard]] static std::filesystem::path GetSettingsFilePath();

    /// <summary>지속되는 에디터 설정의 현재 값이다. 셸과 패널이 부팅 시 되살릴 값을 읽는다.</summary>
    [[nodiscard]] const GameEngine::App::EditorSettingsData& GetSettings() const
    {
        return mSettingsStore.Get();
    }

    /// <summary>패널 배치가 바뀌었음을 알린다. 이전 값과 같으면 저장하지 않는다.</summary>
    /// <param name="panelInSlot">슬롯 i에 놓인 패널 번호들이다.</param>
    void UpdatePanelLayoutSetting(std::vector<std::size_t> panelInSlot);

    /// <summary>
    /// 콘솔 창이 떠 있는지와 그 자리를 설정에 남긴다. 값이 그대로면 파일을 쓰지 않는다.
    ///
    /// 사람이 배치한 것이 실행 때마다 사라지면 창을 옮기는 기능 자체를 쓰지 않게 된다.
    /// </summary>
    /// 사각형이 아니라 수 넷을 받는다. 이 문서 모델은 UI를 모르는 것이 규칙이고, 설정 파일에
    /// 적히는 것도 수 넷이다 — 사각형 타입을 여기까지 끌고 오면 그 규칙이 무너진다.
    /// </summary>
    /// <param name="floating">떠 있으면 true다.</param>
    /// <param name="x">창 왼쪽 변이다. 논리 픽셀이다.</param>
    /// <param name="y">창 위쪽 변이다. 논리 픽셀이다.</param>
    /// <param name="width">창의 폭이다. 논리 픽셀이다.</param>
    /// <param name="height">창의 높이다. 논리 픽셀이다.</param>
    void UpdateConsoleWindowSetting(bool floating, float x, float y, float width, float height);

    /// <summary>씬 뷰 카메라가 바뀌었음을 알린다. 이전 값과 같으면 저장하지 않는다.</summary>
    /// <param name="pivot">궤도의 중심점이다.</param>
    /// <param name="distance">중심점까지의 거리이다.</param>
    /// <param name="yawDegrees">요 각도이다. 도 단위다.</param>
    /// <param name="pitchDegrees">피치 각도이다. 도 단위다.</param>

    /// <summary>격자 스냅을 켜거나 끈다. 값이 같으면 저장하지 않는다.</summary>
    /// <param name="enabled">스냅을 켜려면 true이다.</param>
    void SetGridSnapEnabled(bool enabled);
    // 이 아래는 카메라·패널 배치와 같은 사용자 상태다.
    void UpdateSceneCameraSetting(
        const GameEngine::Math::Vector3& pivot, float distance, float yawDegrees,
        float pitchDegrees);

private:
    /// <summary>
    /// 프로젝트 옆의 스키마 파일을 읽는다. 없거나 읽지 못하면 목록은 비고, 무엇이 없어
    /// 무엇을 할 수 없는지는 로그가 말한다 — 이 에디터를 혼자 쓰는 사람에게는 그 문장이
    /// 유일한 단서다.
    /// </summary>
    void LoadGameComponentSchemas(const std::filesystem::path& projectRoot);

    /// <summary>문서가 일할 때 넘기는 프로젝트다. 문서는 이것을 쥐지 않고 호출마다 받는다.</summary>
    [[nodiscard]] EditorSceneDocument::ProjectHandles MakeDocumentHandles() const;

    /// <summary>별칭 사슬을 끝까지 따라간 현재 id다. 지나간 마디는 끝으로 눌러 둔다(경로 압축).</summary>
    [[nodiscard]] unsigned int ResolveObjectId(unsigned int instanceId) const;

    std::optional<GameEngine::App::ProjectFileData> mOpenProject;
    /// <summary>에디터 자신의 배포 디렉터리이다. 데이터베이스보다 먼저 선언되어 더 오래 산다.</summary>
    std::unique_ptr<GameEngine::Platform::DirectoryContentSource> mEditorContent;
    std::unique_ptr<GameEngine::Assets::AssetDatabase> mEditorAssetDatabase;
    /// <summary>열린 프로젝트의 콘텐츠 소스이다. 런타임이 이것을 통해 읽으므로 런타임보다 먼저 선언되어 더 오래 산다.</summary>
    std::unique_ptr<GameEngine::Platform::DirectoryContentSource> mProjectContent;
    /// <summary>편집 대상 프로젝트의 런타임이다. OpenProject마다 새로 세운다.</summary>
    std::unique_ptr<GameEngine::Runtime::Game> mProjectGame;
    unsigned int mProjectRevision = 0;
    /// <summary>
    /// 열린 프로젝트 루트의 변동을 듣고 언제 다시 읽을지 답한다. 규칙은 그 클래스 주석에 있다.
    /// </summary>
    EditorProjectWatch mProjectWatch;

    /// <summary>
    /// 지금 편집 중인 문서다: 열린 장면, 고른 것, 저장되지 않은 편집, 되돌리기, 플레이. 다섯이
    /// 한 수명을 사는 이유와 프로젝트를 인자로 받는 이유는 그 클래스 주석에 있다.
    /// </summary>
    EditorSceneDocument mDocument;
    /// <summary>
    /// 열린 프로젝트의 게임 컴포넌트 스키마다. 프로젝트를 열 때 채워지고, 다른 프로젝트를
    /// 열면 그 프로젝트의 것으로 갈린다.
    /// </summary>
    std::vector<GameEngine::Serialization::ComponentSchema> mGameComponentSchemas;
    /// <summary>
    /// 에셋을 정돈하는 일과 그 조사 결과다. 프로젝트는 이 부품의 상태가 아니라 호출마다
    /// 넘기는 인자이며, 규칙은 그 클래스 주석에 있다.
    /// </summary>
    EditorAssetMaintenance mAssetMaintenance;

    /// <summary>지속되는 설정과 그것을 파일에 남기는 일이다. 자세한 규칙은 그 클래스 주석에 있다.</summary>
    EditorSettingsStore mSettingsStore;
};

}
