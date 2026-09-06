#pragma once
#include <memory>
#include <optional>
#include <string>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

#include "Component.h"

namespace GameEngine::Runtime
{

class Transform;
class Scene;
class ObjectRegistry;
class RuntimeContext;

/// <summary>장면 안에서 컴포넌트를 소유하고 업데이트하는 런타임 객체이다.</summary>
class GameObject final : public Object
{
public:
    /// <summary>이름을 지정하고 필수 Transform 컴포넌트를 생성한다.</summary>
    /// <param name="name">게임 오브젝트의 이름이다.</param>
    explicit GameObject(RuntimeContext& runtimeContext, std::string name = {});
    ~GameObject() override;

    /// <summary>이름, 활성 상태 및 Transform 값을 복제한 새 게임 오브젝트를 생성한다.</summary>
    /// <returns>새 인스턴스 ID를 가진 게임 오브젝트의 소유 포인터이다.</returns>
    [[nodiscard]] std::unique_ptr<GameObject> Clone() const;

    /// <summary>활성화된 컴포넌트의 수명주기와 프레임 업데이트를 실행한다.</summary>
    /// <param name="deltaTime">이전 프레임부터 흐른 초 단위 시간이다.</param>
    void Update(float deltaTime);

    /// <summary>게임 오브젝트의 활성 상태를 확인한다.</summary>
    /// <returns>컴포넌트 업데이트가 활성화되어 있으면 true이다.</returns>
    [[nodiscard]] bool IsActive() const { return mIsActive; }

    /// <summary>자신과 모든 부모가 활성 상태인지 확인한다.</summary>
    [[nodiscard]] bool IsActiveInHierarchy() const;

    /// <summary>게임 오브젝트의 컴포넌트 업데이트 활성 상태를 변경한다.</summary>
    /// <param name="isActive">업데이트를 허용하려면 true이다.</param>
    void SetActive(const bool isActive) { mIsActive = isActive; }

    /// <summary>이 게임 오브젝트를 소유한 Scene을 반환한다.</summary>
    [[nodiscard]] Scene* GetScene() const { return mScene; }

    /// <summary>
    /// 이 오브젝트가 장면 파일에서 읽힌 id다. 파일에서 읽지 않았으면(새로 만든 오브젝트) 값이
    /// 없다.
    ///
    /// <see cref="GetInstanceId"/>와 다른 것이다: 그것은 이 프로세스의 레지스트리가 매긴 번호라
    /// 다시 여는 세션마다 달라질 수 있다. 이것은 파일이 이 오브젝트를 가리키던 번호이고, 저장이
    /// 그 번호를 그대로 돌려주는 데만 쓰인다 — 오브젝트의 안정된 정체성 체계가 아니다. 그런
    /// 체계(씬을 건너거나 프리팹을 가리키는 참조)가 필요해지면 그때 따로 만든다.
    /// </summary>
    [[nodiscard]] std::optional<unsigned int> GetSerializedId() const { return mSerializedId; }

    /// <summary>로더가 파일에서 읽은 id를 적어 둔다. 그 밖의 자리에서는 부르지 않는다.</summary>
    void SetSerializedId(const unsigned int id) { mSerializedId = id; }

    /// <summary>
    /// 이 객체의 컴포넌트들이 닿을 수 있는 엔진 서비스들이다. `MonoBehaviour`가 — 일부러 갖지
    /// 않는 — `Game`으로의 길 없이 입력에 닿는 방법이 이것이다.
    /// </summary>
    [[nodiscard]] RuntimeContext& GetRuntimeContext() const { return *mRuntimeContext; }

    /// <summary>필수 Transform 컴포넌트를 반환한다.</summary>
    /// <returns>이 게임 오브젝트가 소유한 Transform이다.</returns>
    [[nodiscard]] Transform& GetTransform() { return *mTransform; }

    /// <summary>필수 Transform 컴포넌트를 읽기 전용으로 반환한다.</summary>
    /// <returns>이 게임 오브젝트가 소유한 Transform이다.</returns>
    [[nodiscard]] const Transform& GetTransform() const { return *mTransform; }

    /// <summary>이미 생성된 컴포넌트의 소유권을 넘겨받아 부착한다.</summary>
    /// <param name="component">부착할 컴포넌트의 소유 포인터이다.</param>
    /// <returns>부착된 컴포넌트의 비소유 포인터이며, 부착할 수 없으면 nullptr이다.</returns>
    Component* AddComponent(std::unique_ptr<Component> component);

    /// <summary>지정한 컴포넌트를 제거하고 파괴 콜백을 실행한다.</summary>
    /// <param name="component">제거할 컴포넌트의 비소유 포인터이다.</param>
    /// <returns>제거 요청을 받아들였으면 true이다.</returns>
    /// <remarks>업데이트나 수명주기 콜백 중에는 제거가 지연되며, 필수 Transform은 제거할 수 없다.</remarks>
    [[nodiscard]] bool RemoveComponent(Component* component);

    /// <summary>지정한 형식의 컴포넌트를 생성하고 부착한다.</summary>
    /// <typeparam name="T">Component를 상속한 구체 형식이다.</typeparam>
    /// <typeparam name="Args">컴포넌트 생성자 인수 형식이다.</typeparam>
    /// <param name="args">컴포넌트 생성자에 전달할 인수이다.</param>
    /// <returns>부착된 컴포넌트의 비소유 포인터이며, 부착할 수 없으면 nullptr이다.</returns>
    template <typename T, typename... Args>
    [[nodiscard]] T* AddComponent(Args&&... args);

    /// <summary>지정한 형식과 호환되는 첫 번째 컴포넌트를 찾는다.</summary>
    /// <typeparam name="T">검색할 Component 파생 형식이다.</typeparam>
    /// <returns>찾은 컴포넌트의 비소유 포인터이며, 없으면 nullptr이다.</returns>
    template <typename T>
    [[nodiscard]] T* GetComponent();

    /// <summary>지정한 형식과 호환되는 첫 번째 컴포넌트를 읽기 전용으로 찾는다.</summary>
    /// <typeparam name="T">검색할 Component 파생 형식이다.</typeparam>
    /// <returns>찾은 컴포넌트의 비소유 포인터이며, 없으면 nullptr이다.</returns>
    template <typename T>
    [[nodiscard]] const T* GetComponent() const;

    /// <summary>
    /// 지정한 형식과 호환되는 모든 컴포넌트를 찾는다.
    ///
    /// 복사된 벡터가 아니라 이 객체의 컴포넌트에 대한 지연 뷰를 반환하므로, 한 번만 순회하는
    /// 호출자 — 모든 호출자가 그렇다 — 는 아무것도 할당하지 않는다. 렌더 프론트엔드가 프레임마다
    /// 모든 객체에 이것을 네 번 묻으므로, 벡터를 반환하면 객체당·프레임당 힙 할당 네 번이 된다.
    ///
    /// 뷰는 순회하면서 컴포넌트 목록을 읽으므로, 뷰를 순회하는 동안 컴포넌트를 붙이거나 떼지
    /// 마라. 이는 타입의 나머지와 어울린다: 수명주기 콜백 중의 제거는 이미 제자리 적용이 아니라
    /// 지연 제거 목록을 통해 미뤄진다.
    /// </summary>
    template <typename T>
    [[nodiscard]] auto GetComponents();

    /// <summary>지정한 형식과 호환되는 모든 컴포넌트를 읽기 전용으로 찾는다.</summary>
    template <typename T>
    [[nodiscard]] auto GetComponents() const;

    /// <summary>이 게임 오브젝트가 소유한 모든 컴포넌트를 반환한다.</summary>
    [[nodiscard]] const std::vector<std::unique_ptr<Component>>& GetAllComponents() const
    {
        return mComponents;
    }

private:
    friend class Scene;

    void SetScene(Scene* scene) { mScene = scene; }
    [[nodiscard]] bool IsPendingRemoval(const Component* component) const;
    void FlushPendingRemovals();
    [[nodiscard]] bool DestroyComponent(Component* component);

    std::vector<std::unique_ptr<Component>> mComponents;
    std::vector<Component*> mPendingComponentRemovals;
    RuntimeContext* mRuntimeContext = nullptr;
    Transform* mTransform = nullptr;
    Scene* mScene = nullptr;
    std::optional<unsigned int> mSerializedId;
    bool mIsActive = true;
    bool mIsUpdating = false;
    bool mIsDispatchingLifecycle = false;
    bool mIsDestroying = false;
};

template <typename T, typename... Args>
T* GameObject::AddComponent(Args&&... args)
{
    static_assert(std::is_base_of_v<Component, T>, "T must derive from Component.");
    return static_cast<T*>(AddComponent(std::make_unique<T>(std::forward<Args>(args)...)));
}

namespace Detail
{
    /// <summary>
    /// 컴포넌트가 요청된 클래스이거나 그것을 상속하는지 여부이다.
    ///
    /// 선언된 컴포넌트 타입이 "요청된 클래스인가"에 답하므로 `dynamic_cast`가 필요 없고, 남는
    /// 것은 그 답이 이미 안전하게 만들어 준 `static_cast`뿐이다.
    /// </summary>
    template <typename T>
    [[nodiscard]] inline bool IsComponentOfType(const Component& component)
    {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component.");
        return component.GetComponentType().IsDerivedFrom(T::StaticType());
    }
}

template <typename T>
T* GameObject::GetComponent()
{
    for (const std::unique_ptr<Component>& component : mComponents)
    {
        if (Detail::IsComponentOfType<T>(*component))
        {
            return static_cast<T*>(component.get());
        }
    }

    return nullptr;
}

template <typename T>
const T* GameObject::GetComponent() const
{
    for (const std::unique_ptr<Component>& component : mComponents)
    {
        if (Detail::IsComponentOfType<T>(*component))
        {
            return static_cast<const T*>(component.get());
        }
    }

    return nullptr;
}

template <typename T>
auto GameObject::GetComponents()
{
    return mComponents
        | std::views::filter([](const std::unique_ptr<Component>& component)
            {
                return Detail::IsComponentOfType<T>(*component);
            })
        | std::views::transform([](const std::unique_ptr<Component>& component)
            {
                return static_cast<T*>(component.get());
            });
}

template <typename T>
auto GameObject::GetComponents() const
{
    return mComponents
        | std::views::filter([](const std::unique_ptr<Component>& component)
            {
                return Detail::IsComponentOfType<T>(*component);
            })
        | std::views::transform([](const std::unique_ptr<Component>& component)
            {
                return static_cast<const T*>(component.get());
            });
}

}
