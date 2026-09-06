#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "GameObject.h"

namespace GameEngine::Runtime
{

class ObjectRegistry;
class RuntimeContext;

/// <summary>게임 오브젝트 집합의 소유권과 프레임 업데이트를 관리한다.</summary>
class Scene
{
public:
    explicit Scene(RuntimeContext& runtimeContext, std::string name = {});
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;
    /// <summary>
    /// 종료 콜백 전에 모든 객체를 조회 목록에서 빼고 Scene 연결을 끊는다. 종료 중 이 장면을
    /// 직접 보관한 콜백의 조회는 빈 결과, 제거는 false, 추가는 nullptr를 반환한다.
    /// </summary>
    virtual ~Scene();

    /// <summary>소속 게임 오브젝트를 복제해 독립적인 장면을 생성한다.</summary>
    /// <returns>새 인스턴스 ID를 사용하는 복제 장면의 소유 포인터이다.</returns>
    [[nodiscard]] std::unique_ptr<Scene> Clone() const;

    /// <summary>장면에 속한 모든 게임 오브젝트를 업데이트한다.</summary>
    /// <param name="deltaTime">이전 프레임부터 흐른 초 단위 시간이다.</param>
    void Update(float deltaTime);

    /// <summary>게임 오브젝트를 생성해 장면에 추가한다.</summary>
    [[nodiscard]] GameObject* CreateGameObject(std::string name = {});

    /// <summary>게임 오브젝트의 소유권을 장면으로 이전한다.</summary>
    /// <param name="gameObject">추가할 게임 오브젝트의 소유 포인터이다.</param>
    /// <returns>추가된 게임 오브젝트의 비소유 포인터이며, 입력이 nullptr이면 nullptr이다.</returns>
    GameObject* AddGameObject(std::unique_ptr<GameObject> gameObject);

    /// <summary>
    /// 대상과 이 장면에 속한 모든 자손을 제거한다. 이번 프레임에 추가한 자손도 포함한다.
    /// Update 또는 삭제 콜백 중이면 제거를 보류하며, flush 전까지는 대상을 조회할 수 있다.
    /// </summary>
    /// <param name="instanceId">제거할 게임 오브젝트의 인스턴스 ID이다.</param>
    /// <returns>대상을 찾아 제거했으면 true이다.</returns>
    bool RemoveGameObject(unsigned int instanceId);

    /// <summary>인스턴스 ID로 게임 오브젝트를 찾는다.</summary>
    /// <param name="instanceId">찾을 게임 오브젝트의 인스턴스 ID이다.</param>
    /// <returns>찾은 게임 오브젝트의 비소유 포인터이며, 없으면 nullptr이다.</returns>
    [[nodiscard]] GameObject* GetGameObject(unsigned int instanceId);

    /// <summary>인스턴스 ID로 게임 오브젝트를 읽기 전용으로 찾는다.</summary>
    /// <param name="instanceId">찾을 게임 오브젝트의 인스턴스 ID이다.</param>
    /// <returns>찾은 게임 오브젝트의 비소유 포인터이며, 없으면 nullptr이다.</returns>
    [[nodiscard]] const GameObject* GetGameObject(unsigned int instanceId) const;

    /// <summary>장면이 논리적으로 포함하는 GameObject 또는 Component를 ID로 찾는다.</summary>
    [[nodiscard]] Object* FindObject(unsigned int instanceId);
    [[nodiscard]] const Object* FindObject(unsigned int instanceId) const;

    /// <summary>장면의 GameObject와 그 소유 컴포넌트를 모두 반환한다.</summary>
    [[nodiscard]] std::vector<Object*> GetObjects();
    [[nodiscard]] std::vector<const Object*> GetObjects() const;

    /// <summary>부모가 없는 장면 최상위 게임 오브젝트를 반환한다.</summary>
    [[nodiscard]] std::vector<GameObject*> GetRootGameObjects();
    [[nodiscard]] std::vector<const GameObject*> GetRootGameObjects() const;

    [[nodiscard]] GameObject* FindGameObject(const std::string& name);
    [[nodiscard]] const GameObject* FindGameObject(const std::string& name) const;

    [[nodiscard]] const std::string& GetName() const { return mName; }

    // GetObjectRegistry는 두지 않는다. 레지스트리가 여는 연산은 등록과 해제 — 객체 수명 관리 —
    // 이고, 장면을 통해 그것을 내주면 컴포넌트가 자기 GameObject를 거쳐 거기 닿는다.
    // RuntimeContext가 같은 이유로 그것을 감춘다.

    /// <summary>이 장면의 객체들이 닿을 수 있는 엔진 서비스들이다.</summary>
    [[nodiscard]] RuntimeContext& GetRuntimeContext() const { return *mRuntimeContext; }
    void SetName(std::string name) { mName = std::move(name); }

    /// <summary>장면이 소유한 모든 게임 오브젝트를 반환한다.</summary>
    /// <returns>인스턴스 ID와 게임 오브젝트 소유 포인터의 대응표이다.</returns>
    // 등록된 저장소이며 추가 대기 객체는 제외한다. GetObjects/FindObject는 추가 대기 객체도 포함한다.
    [[nodiscard]] const std::unordered_map<unsigned int, std::unique_ptr<GameObject>>& GetGameObjects() const
    {
        return mGameObjects;
    }

private:
    void QueueHierarchyRemoval(const GameObject& gameObject);
    void FlushPendingChanges();

    /// <summary>
    /// 장면이 지금 담고 있는 게임 오브젝트를 하나씩 방문한다. 순서는 업데이트가 보는 바로 그
    /// 순서다: 이미 들어온 것들이 들어온 순서대로, 그다음 이번 프레임에 추가돼 아직 반영되지
    /// 않은 것들이다.
    ///
    /// 열거하는 자리마다 그 순서와 "pending 추가분도 장면의 일부다"라는 규칙을 손으로 다시
    /// 적지 않고 이 함수를 쓴다. 한 곳만 어긋나도 — 예컨대 pending을 빠뜨리면 — 방금 만든
    /// 객체가 그 함수에만 보이지 않는, 찾기 어려운 종류의 버그가 된다.
    ///
    /// 제거 예정으로 표시된 객체는 걸러지지 않는다. 그것은 <see cref="Update"/>만의 규칙이며,
    /// 표시된 객체도 flush 전까지는 살아 있고 이 장면의 것이다.
    /// </summary>
    /// <param name="visit">게임 오브젝트 하나를 받는 호출 가능 객체다.</param>
    template <typename Visitor>
    void ForEachGameObject(Visitor&& visit)
    {
        VisitGameObjects(*this, visit);
    }

    /// <summary>읽기 전용 순회다. 순서 규칙은 위와 같다.</summary>
    template <typename Visitor>
    void ForEachGameObject(Visitor&& visit) const
    {
        VisitGameObjects(*this, visit);
    }

    /// <summary>
    /// 아직 반영되지 않은 추가분에서 이 id의 객체를 찾는다. 없으면 끝 반복자다. 반복자를
    /// 돌려주는 것은 <see cref="RemoveGameObject"/>가 찾은 자리를 지워야 하기 때문이다.
    /// </summary>
    [[nodiscard]] std::vector<std::unique_ptr<GameObject>>::iterator FindPendingAddition(
        unsigned int instanceId);
    [[nodiscard]] std::vector<std::unique_ptr<GameObject>>::const_iterator FindPendingAddition(
        unsigned int instanceId) const;

    /// <summary>
    /// const와 비const 순회가 같은 한 벌이 되게 하는 자리다. C++20에는 deducing this가 없어
    /// 대신 자신을 인수로 받는다.
    /// </summary>
    template <typename SceneSelf, typename Visitor>
    static void VisitGameObjects(SceneSelf& scene, Visitor& visit)
    {
        for (const unsigned int instanceId : scene.mUpdateOrder)
        {
            visit(*scene.mGameObjects.at(instanceId));
        }
        for (auto& gameObject : scene.mPendingAdditions)
        {
            visit(*gameObject);
        }
    }

    RuntimeContext* mRuntimeContext = nullptr;
    std::string mName;
    std::unordered_map<unsigned int, std::unique_ptr<GameObject>> mGameObjects;
    /// <summary>
    /// 객체가 장면에 들어온 순서다. 업데이트와 열거가 이 순서를 따른다: 해시 컨테이너의 순서는
    /// 프로세스마다 다를 수 있어, "A가 B보다 먼저 업데이트된다"에 기대는 로직의 근거가 되지
    /// 못한다. 로드된 장면은 파일의 객체 순서(인스턴스 id 순)가 곧 이 순서다.
    /// </summary>
    std::vector<unsigned int> mUpdateOrder;
    std::vector<std::unique_ptr<GameObject>> mPendingAdditions;
    std::unordered_set<unsigned int> mPendingRemovals;
    bool mIsUpdating = false;
    bool mIsFlushingChanges = false;
    bool mIsDestroying = false;
};

}
