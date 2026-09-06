#include "pch.h"
#include "RuntimeContext.h"
#include "Scene.h"

#include "Transform.h"
#include "../Diagnostics/Debug.h"

#include <algorithm>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace GameEngine::Runtime
{

Scene::Scene(RuntimeContext& runtimeContext, std::string name)
    : mRuntimeContext(&runtimeContext), mName(std::move(name))
{
}

Scene::~Scene()
{
    mIsDestroying = true;
    // 지역 소유자는 모든 조회 컨테이너가 살아 있는 동안 콜백을 마친다. 객체를 먼저 분리하고
    // 전체 연결을 끊어야 형제의 콜백도 종료 중인 장면으로 다시 들어오지 않는다.
    auto removed = std::move(mGameObjects);
    auto pending = std::move(mPendingAdditions);
    mGameObjects.clear();
    mPendingAdditions.clear();
    mUpdateOrder.clear();
    mPendingRemovals.clear();
    for (const auto& entry : removed)
    {
        entry.second->SetScene(nullptr);
    }
    for (const auto& gameObject : pending)
    {
        gameObject->SetScene(nullptr);
    }
}

std::unique_ptr<Scene> Scene::Clone() const
{
    auto clonedScene = std::make_unique<Scene>(*mRuntimeContext, mName);
    std::unordered_map<const Transform*, Transform*> clonedTransforms;

    // 아직 반영되지 않은 추가분도 이 장면의 것이므로 함께 복제된다.
    ForEachGameObject([&clonedScene, &clonedTransforms](GameObject& gameObject)
    {
        auto clonedGameObject = gameObject.Clone();
        clonedTransforms.emplace(&gameObject.GetTransform(), &clonedGameObject->GetTransform());
        static_cast<void>(clonedScene->AddGameObject(std::move(clonedGameObject)));
    });

    for (const auto& [sourceTransform, clonedTransform] : clonedTransforms)
    {
        const Transform* sourceParent = sourceTransform->GetParent();
        if (sourceParent && clonedTransforms.contains(sourceParent))
        {
            (void)clonedTransform->SetParent(clonedTransforms.at(sourceParent));
        }
    }

    return clonedScene;
}

void Scene::Update(float deltaTime)
{
    if (mIsDestroying || mGameObjects.empty())
    {
        return;
    }

    mIsUpdating = true;
    // 들어온 순서대로. 업데이트 중의 추가·삭제는 보류되므로 이 목록은 순회 중에 바뀌지 않는다.
    for (const unsigned int instanceId : mUpdateOrder)
    {
        if (mPendingRemovals.contains(instanceId))
        {
            continue;
        }
        if (const auto iterator = mGameObjects.find(instanceId); iterator != mGameObjects.end())
        {
            iterator->second->Update(deltaTime);
        }
    }
    mIsUpdating = false;
    FlushPendingChanges();
}

GameObject* Scene::CreateGameObject(std::string name)
{
    if (mIsDestroying)
    {
        return nullptr;
    }
    return AddGameObject(std::make_unique<GameObject>(*mRuntimeContext, std::move(name)));
}

GameObject* Scene::AddGameObject(std::unique_ptr<GameObject> gameObject)
{
    if (mIsDestroying || !gameObject)
    {
        return nullptr;
    }

    const unsigned int instanceId = gameObject->GetInstanceId();
    GameObject* rawPointer = gameObject.get();

    if (mGameObjects.contains(instanceId) ||
        FindPendingAddition(instanceId) != mPendingAdditions.end())
    {
        Diagnostics::Debug::LogError("Cannot add a duplicate GameObject instance ID to a Scene.");
        return nullptr;
    }

    if (gameObject->GetScene())
    {
        Diagnostics::Debug::LogError("A GameObject cannot belong to more than one Scene.");
        return nullptr;
    }

    Transform& transform = gameObject->GetTransform();
    Transform* parent = transform.GetParent();
    if (parent && parent->GetGameObject() && parent->GetGameObject()->GetScene() &&
        parent->GetGameObject()->GetScene() != this)
    {
        Diagnostics::Debug::LogError("A Transform hierarchy cannot cross Scene boundaries.");
        return nullptr;
    }
    if (std::ranges::any_of(
        transform.GetChildren(),
        [this](const Transform* child)
        {
            return child->GetGameObject() && child->GetGameObject()->GetScene() &&
                child->GetGameObject()->GetScene() != this;
        }))
    {
        Diagnostics::Debug::LogError("A Transform hierarchy cannot cross Scene boundaries.");
        return nullptr;
    }

    if (mIsUpdating || mIsFlushingChanges)
    {
        gameObject->SetScene(this);
        mPendingAdditions.push_back(std::move(gameObject));
    }
    else
    {
        gameObject->SetScene(this);
        mGameObjects.emplace(instanceId, std::move(gameObject));
        mUpdateOrder.push_back(instanceId);
    }

    Diagnostics::Debug::Log("Added GameObject " + std::to_string(instanceId));

    return rawPointer;
}

std::vector<std::unique_ptr<GameObject>>::iterator Scene::FindPendingAddition(
    const unsigned int instanceId)
{
    return std::ranges::find_if(
        mPendingAdditions,
        [instanceId](const std::unique_ptr<GameObject>& gameObject)
        {
            return gameObject->GetInstanceId() == instanceId;
        });
}

std::vector<std::unique_ptr<GameObject>>::const_iterator Scene::FindPendingAddition(
    const unsigned int instanceId) const
{
    return std::ranges::find_if(
        mPendingAdditions,
        [instanceId](const std::unique_ptr<GameObject>& gameObject)
        {
            return gameObject->GetInstanceId() == instanceId;
        });
}

bool Scene::RemoveGameObject(const unsigned int instanceId)
{
    if (mIsDestroying)
    {
        return false;
    }
    const GameObject* const gameObject = GetGameObject(instanceId);
    if (!gameObject)
    {
        return false;
    }

    QueueHierarchyRemoval(*gameObject);
    if (!mIsUpdating && !mIsFlushingChanges)
    {
        FlushPendingChanges();
    }
    return true;
}

void Scene::QueueHierarchyRemoval(const GameObject& gameObject)
{
    mPendingRemovals.insert(gameObject.GetInstanceId());
    // GetGameObject는 본 목록과 이번 프레임 추가분을 모두 본다. pending 자식만 제외하면
    // 부모를 지운 뒤 새 자식이 루트로 살아나는 결과가 된다.
    for (const Transform* child : gameObject.GetTransform().GetChildren())
    {
        const GameObject* const childObject = child ? child->GetGameObject() : nullptr;
        if (childObject && GetGameObject(childObject->GetInstanceId()) == childObject)
        {
            QueueHierarchyRemoval(*childObject);
        }
    }
}

GameObject* Scene::GetGameObject(const unsigned int instanceId)
{
    const auto iterator = mGameObjects.find(instanceId);
    if (iterator != mGameObjects.end())
    {
        return iterator->second.get();
    }

    const auto pendingIterator = FindPendingAddition(instanceId);
    return pendingIterator == mPendingAdditions.end() ? nullptr : pendingIterator->get();
}

const GameObject* Scene::GetGameObject(const unsigned int instanceId) const
{
    const auto iterator = mGameObjects.find(instanceId);
    if (iterator != mGameObjects.end())
    {
        return iterator->second.get();
    }

    const auto pendingIterator = FindPendingAddition(instanceId);
    return pendingIterator == mPendingAdditions.end() ? nullptr : pendingIterator->get();
}

Object* Scene::FindObject(const unsigned int instanceId)
{
    if (GameObject* gameObject = GetGameObject(instanceId))
    {
        return gameObject;
    }

    const auto findComponent = [instanceId](GameObject& gameObject) -> Component*
    {
        for (const std::unique_ptr<Component>& component : gameObject.GetAllComponents())
        {
            if (component->GetInstanceId() == instanceId)
            {
                return component.get();
            }
        }
        return nullptr;
    };
    Component* found = nullptr;
    ForEachGameObject([&found, &findComponent](GameObject& gameObject)
    {
        if (!found)
        {
            found = findComponent(gameObject);
        }
    });
    return found;
}

const Object* Scene::FindObject(const unsigned int instanceId) const
{
    if (const GameObject* gameObject = GetGameObject(instanceId))
    {
        return gameObject;
    }

    const auto findComponent = [instanceId](const GameObject& gameObject) -> const Component*
    {
        for (const std::unique_ptr<Component>& component : gameObject.GetAllComponents())
        {
            if (component->GetInstanceId() == instanceId)
            {
                return component.get();
            }
        }
        return nullptr;
    };
    const Component* found = nullptr;
    ForEachGameObject([&found, &findComponent](const GameObject& gameObject)
    {
        if (!found)
        {
            found = findComponent(gameObject);
        }
    });
    return found;
}

std::vector<Object*> Scene::GetObjects()
{
    std::vector<Object*> objects;
    const auto appendGameObject = [&objects](GameObject& gameObject)
    {
        objects.push_back(&gameObject);
        for (const std::unique_ptr<Component>& component : gameObject.GetAllComponents())
        {
            objects.push_back(component.get());
        }
    };

    ForEachGameObject(appendGameObject);
    return objects;
}

std::vector<const Object*> Scene::GetObjects() const
{
    std::vector<const Object*> objects;
    const auto appendGameObject = [&objects](const GameObject& gameObject)
    {
        objects.push_back(&gameObject);
        for (const std::unique_ptr<Component>& component : gameObject.GetAllComponents())
        {
            objects.push_back(component.get());
        }
    };

    ForEachGameObject(appendGameObject);
    return objects;
}

std::vector<GameObject*> Scene::GetRootGameObjects()
{
    std::vector<GameObject*> roots;
    const auto appendIfRoot = [this, &roots](GameObject& gameObject)
    {
        Transform* parent = gameObject.GetTransform().GetParent();
        if (!parent || !parent->GetGameObject() || parent->GetGameObject()->GetScene() != this)
        {
            roots.push_back(&gameObject);
        }
    };

    ForEachGameObject(appendIfRoot);
    return roots;
}

std::vector<const GameObject*> Scene::GetRootGameObjects() const
{
    std::vector<const GameObject*> roots;
    const auto appendIfRoot = [this, &roots](const GameObject& gameObject)
    {
        const Transform* parent = gameObject.GetTransform().GetParent();
        if (!parent || !parent->GetGameObject() || parent->GetGameObject()->GetScene() != this)
        {
            roots.push_back(&gameObject);
        }
    };

    ForEachGameObject(appendIfRoot);
    return roots;
}

GameObject* Scene::FindGameObject(const std::string& name)
{
    // 이름으로 찾는 것은 순회 순서를 따른다. 해시 컨테이너를 훑으면 같은 이름이 둘 있는
    // 장면에서 어느 쪽이 나오는지가 프로세스마다 달라진다 — 업데이트 순서를 따로 두는
    // 이유와 같다.
    GameObject* found = nullptr;
    ForEachGameObject([&found, &name](GameObject& gameObject)
    {
        if (!found && gameObject.GetName() == name)
        {
            found = &gameObject;
        }
    });
    return found;
}

const GameObject* Scene::FindGameObject(const std::string& name) const
{
    const GameObject* found = nullptr;
    ForEachGameObject([&found, &name](const GameObject& gameObject)
    {
        if (!found && gameObject.GetName() == name)
        {
            found = &gameObject;
        }
    });
    return found;
}

void Scene::FlushPendingChanges()
{
    if (mIsFlushingChanges) return;
    struct FlushGuard
    {
        bool& active;
        explicit FlushGuard(bool& value) : active(value) { active = true; }
        ~FlushGuard() { active = false; }
    } guard(mIsFlushingChanges);

    while (!mPendingRemovals.empty())
    {
        const unsigned int instanceId = *mPendingRemovals.begin();
        if (const GameObject* const gameObject = GetGameObject(instanceId))
        {
            // 삭제 요청 이후 같은 Update/OnDestroy에서 붙은 자손도 놓치지 않는다.
            QueueHierarchyRemoval(*gameObject);
        }
        mPendingRemovals.erase(instanceId);

        std::unique_ptr<GameObject> removed;
        if (const auto iterator = mGameObjects.find(instanceId); iterator != mGameObjects.end())
        {
            removed = std::move(iterator->second);
            mGameObjects.erase(iterator);
            std::erase(mUpdateOrder, instanceId);
        }
        else if (const auto pending = FindPendingAddition(instanceId); pending != mPendingAdditions.end())
        {
            removed = std::move(*pending);
            mPendingAdditions.erase(pending);
        }

        // 콜백 전에 컨테이너에서 소유권과 id를 빼 둔다. OnDestroy가 자신이나 다른 객체를
        // 다시 지우거나 새 객체를 추가해도 이 파괴와 컨테이너 순회를 재진입하지 않는다.
        if (removed) removed->SetScene(nullptr);
        removed.reset();
    }

    for (std::unique_ptr<GameObject>& gameObject : mPendingAdditions)
    {
        const unsigned int instanceId = gameObject->GetInstanceId();
        mGameObjects.emplace(instanceId, std::move(gameObject));
        mUpdateOrder.push_back(instanceId);
    }
    mPendingAdditions.clear();
}

}
