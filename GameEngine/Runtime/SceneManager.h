#pragma once

#include "SceneLoader.h"

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Scene.h"
#include "../Platform/IContentSource.h"

namespace GameEngine::Runtime
{

enum class SceneLoadResult
{
    Failed,
    Loaded,
    Queued
};

/// <summary>배포된 장면 경로와 활성 장면의 수명을 관리한다.</summary>
class SceneManager final
{
public:
    explicit SceneManager(RuntimeContext& runtimeContext);

    /// <summary>장면 파일을 읽을 로더를 둔다. 없으면 파일에서의 장면 로드는 실패하고 로그로 말한다.</summary>
    void SetSceneLoader(SceneLoader loader) { mSceneLoader = std::move(loader); }
    ~SceneManager();

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    /// <summary>
    /// 어느 파일이 어느 장면을 담는지와, 그것들을 읽어 올 소스를 기록한다. 경로는 그 소스 기준
    /// 상대로 남으므로, 실행 파일에 packed된 프로젝트도 디렉터리에 놓인 프로젝트와 같은 방식으로
    /// 장면을 로드한다. 소스는 소유하지 않으며 이것보다 오래 살아야 한다.
    /// </summary>
    bool RegisterScenePaths(
        const Platform::IContentSource& source,
        const std::unordered_map<unsigned int, std::filesystem::path>& scenePaths);
    /// <summary>장면 로드를 요청하고 즉시 완료, 지연 예약 또는 실패 상태를 반환한다.</summary>
    [[nodiscard]] SceneLoadResult LoadScene(unsigned int sceneId);

    /// <summary>
    /// 이미 만들어진 장면을 새 id 아래 활성 목록에 넣고 그 id를 반환한다. 실패하면 0이다.
    ///
    /// 등록된 경로에서 id로 로드하는 것과는 다른 질문에 답한다: 에디터는 자기가 연 프로젝트의
    /// 장면 파일을 직접 역직렬화하는데, 그 프로젝트의 장면 id는 에디터 자신의 장면이 이미 차지한
    /// id와 충돌할 수 있다. 여기서 발급하는 id는 등록된 경로가 결코 쓰지 않는 범위에서 나오므로
    /// 충돌이 없고, 반환된 id로 GetScene과 UnloadScene을 그대로 쓸 수 있다.
    ///
    /// 장면들이 업데이트 중일 때는 거부한다: 지금 순회 중인 목록에 넣는 셈이기 때문이다.
    /// </summary>
    [[nodiscard]] unsigned int AddScene(std::unique_ptr<Scene> scene);
    bool UnloadScene(unsigned int sceneId);
    /// <summary>
    /// 지정 장면만 남기고 나머지를 모두 내린다. 종료 콜백 전에 제거 대상 전체를 조회에서 빼고,
    /// 콜백 중의 장면 추가·로드·삭제를 거절한다. 대상이 없거나 갱신·지연 처리 중이면 변경 없이 실패한다.
    /// </summary>
    [[nodiscard]] bool RetainOnlyScene(unsigned int sceneId);
    void Update(float deltaTime);

    /// <summary>
    /// Update 자신이 로드한 장면들이다. 한 번만 넘겨준다.
    ///
    /// 업데이트 중 요청된 로드는 큐에 들어가 Update 끝에서 수행된다. 즉시 경로인
    /// Game::LoadScene이 이 로드를 볼 수 없으므로, 호출자는 이 목록으로 새 장면의 에셋을
    /// 렌더링 전에 미리 로드한다.
    /// </summary>
    [[nodiscard]] std::vector<unsigned int> TakeScenesLoadedDuringUpdate();

    [[nodiscard]] Scene* GetScene(unsigned int sceneId);
    [[nodiscard]] const Scene* GetScene(unsigned int sceneId) const;
    [[nodiscard]] bool IsSceneLoaded(unsigned int sceneId) const;
    [[nodiscard]] const std::unordered_map<unsigned int, std::unique_ptr<Scene>>& GetActiveScenes() const
    {
        return mActiveScenes;
    }

private:
    bool UnloadSceneImmediate(unsigned int sceneId);
    bool LoadSceneImmediate(unsigned int sceneId);
    void FlushPendingChanges();

    [[nodiscard]] std::unique_ptr<Scene> LoadSceneFile(
        const std::filesystem::path& relativeScenePath);

    RuntimeContext* mRuntimeContext = nullptr;
    SceneLoader mSceneLoader;
    const Platform::IContentSource* mContentSource = nullptr;
    std::unordered_map<unsigned int, std::filesystem::path> mScenePaths;
    std::unordered_map<unsigned int, std::unique_ptr<Scene>> mActiveScenes;
    std::vector<unsigned int> mPendingLoads;
    std::vector<unsigned int> mScenesLoadedDuringUpdate;

    /// <summary>AddScene이 발급할 다음 id이다. 프로젝트 파일의 장면 id가 닿지 않는 범위에서 시작한다.</summary>
    unsigned int mNextAddedSceneId = FirstAddedSceneId;

    /// <summary>AddScene이 발급하는 id의 시작이다. 이 위로는 등록된 경로가 결코 쓰지 않는다.</summary>
    static constexpr unsigned int FirstAddedSceneId = 0x40000000u;
    std::unordered_set<unsigned int> mPendingUnloads;
    bool mIsUpdating = false;
    bool mIsFlushingChanges = false;
    bool mIsDestroying = false;
    bool mIsRetainingScene = false;
};

}
