#include "Document/EditorCommands.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

#include "Rules/EditorObjectHost.h"
#include "Diagnostics/Debug.h"
#include "Runtime/Component.h"
#include "Runtime/ComponentType.h"
#include "Runtime/GameObject.h"
#include "Runtime/Scene.h"
#include "Runtime/Transform.h"
#include "Serialization/PreservedComponent.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Serialization/SceneSerializer.h"

namespace GameEditor
{

namespace
{
    using GameEngine::Runtime::Component;
    using GameEngine::Runtime::ComponentType;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::PropertyDescriptor;
    using GameEngine::Runtime::PropertyValue;

    /// <summary>해석 실패를 로그로 말한다. 스택은 실패한 커맨드를 버리므로, 이 로그가 사람이
    /// "아무 일도 일어나지 않은 undo"를 이해할 유일한 단서다.</summary>
    void LogUnresolvedTarget(const char* const command, const unsigned int instanceId)
    {
        GameEngine::Diagnostics::Debug::LogWarning(
            "An undo command's target no longer resolves; the command is dropped. command=",
            command, ", id=", instanceId);
    }

    [[nodiscard]] const ComponentType* FindEngineComponentType(const std::string& typeName)
    {
        for (const ComponentType* const type : GameEngine::Serialization::EngineComponentTypes())
        {
            if (type->GetName() == typeName)
            {
                return type;
            }
        }
        return nullptr;
    }

    /// <summary>
    /// 스냅숏을 게임 오브젝트에 되세운다. 장면 로드가 컴포넌트를 만드는 바로 그 길을 쓰므로,
    /// 속성이든 타일맵의 격자든 파일에 실리는 상태는 전부 함께 돌아온다. 만들 수 없으면
    /// nullptr이며, 그 이유는 로그가 말한다.
    /// </summary>
    [[nodiscard]] Component* RestoreComponent(
        const ComponentSnapshot& snapshot, GameObject& gameObject)
    {
        if (!snapshot.valid)
        {
            GameEngine::Diagnostics::Debug::LogError(
                "Undo cannot recreate a component the engine cannot serialize. type=",
                snapshot.typeName);
            return nullptr;
        }
        try
        {
            return GameEngine::Serialization::SceneSerializer::LoadComponentIntoGameObject(
                snapshot.json, gameObject);
        }
        catch (const GameEngine::Core::JsonError& error)
        {
            // 우리가 쓴 JSON이 우리 로더에게 거절당했다면 직렬화 자신의 버그다. undo가 UI 루프로
            // 예외를 던지게 두는 대신, 커맨드를 버리고 무엇이 거절됐는지 남긴다.
            GameEngine::Diagnostics::Debug::LogError(
                "Undo could not rebuild a component from its snapshot. type=", snapshot.typeName,
                ", error=", error.what());
            return nullptr;
        }
    }

    /// <summary>
    /// 객체와 그 자손 전체를 부모 우선 순서로 스냅숏한다. Scene::RemoveGameObject가 지우는 것과
    /// 같은 걷기라서, 이 스냅숏이 곧 그 삭제의 되돌림 재료다.
    /// </summary>
    [[nodiscard]] std::vector<GameObjectSnapshot> SnapshotSubtree(GameObject& root)
    {
        std::vector<GameObjectSnapshot> objects;
        const auto collect = [&objects](const auto& self, GameObject& current) -> void
        {
            GameObjectSnapshot snapshot;
            snapshot.id = current.GetInstanceId();
            snapshot.siblingIndex = current.GetTransform().GetSiblingIndex();
            const GameEngine::Runtime::Transform* const parent = current.GetTransform().GetParent();
            if (parent && parent->GetGameObject())
            {
                snapshot.parentId = parent->GetGameObject()->GetInstanceId();
            }
            snapshot.name = current.GetName();
            snapshot.isActive = current.IsActive();
            for (const std::unique_ptr<Component>& component : current.GetAllComponents())
            {
                snapshot.components.emplace_back(
                    component->GetInstanceId(), SnapshotComponent(*component));
            }
            objects.push_back(std::move(snapshot));

            for (GameEngine::Runtime::Transform* const childTransform :
                 current.GetTransform().GetChildren())
            {
                GameObject* const child = childTransform ? childTransform->GetGameObject() : nullptr;
                if (child && child->GetScene() == current.GetScene())
                {
                    self(self, *child);
                }
            }
        };
        collect(collect, root);
        return objects;
    }

    /// <summary>스냅숏의 뿌리가 가리키는 살아 있는 부분 트리를 장면에서 지운다.</summary>
    [[nodiscard]] bool DeleteSubtree(
        IEditorObjectHost& host, const std::vector<GameObjectSnapshot>& objects,
        const char* const command)
    {
        if (objects.empty())
        {
            return false;
        }
        GameObject* const root = host.FindGameObject( objects.front().id);
        GameEngine::Runtime::Scene* const scene = root ? root->GetScene() : nullptr;
        if (!scene)
        {
            LogUnresolvedTarget(command, objects.front().id);
            return false;
        }
        return scene->RemoveGameObject(root->GetInstanceId());
    }

    /// <summary>
    /// 스냅숏에서 부분 트리를 다시 세운다. 전부-또는-없음이다: 객체 생성이 도중에 실패하면 이미
    /// 세운 것을 역순으로 되지우고 별칭 없이 false를 반환한다. 컴포넌트 하나가 복원되지 않는
    /// 것은 로그를 남기고 계속한다 — 객체를 통째로 포기하는 것보다 낫다. 성공했을 때만 옛 id →
    /// 새 id 별칭이 등록되므로, 실패한 복원은 어떤 참조도 바꾸지 않는다.
    /// </summary>
    [[nodiscard]] bool RestoreSubtree(
        IEditorObjectHost& host, const std::vector<GameObjectSnapshot>& objects)
    {
        GameEngine::Runtime::Scene* const scene = host.GetOpenScene();
        if (!scene || objects.empty())
        {
            GameEngine::Diagnostics::Debug::LogWarning(
                "Undo cannot restore objects without an open scene.");
            return false;
        }

        // 옛 id를 그대로 열쇠로 써서 세운다. 스냅숏의 id는 영원히 옛 값이므로 자식의 parentId는
        // 이 맵에서, 뿌리의 바깥 부모는 별칭을 따라 런타임에서 해석된다.
        std::unordered_map<unsigned int, GameObject*> restoredByOldId;
        std::vector<unsigned int> createdIds;
        std::vector<std::pair<unsigned int, unsigned int>> aliases;
        for (const GameObjectSnapshot& snapshot : objects)
        {
            GameObject* const gameObject = scene->CreateGameObject(snapshot.name);
            if (!gameObject)
            {
                GameEngine::Diagnostics::Debug::LogError(
                    "Undo could not recreate a GameObject; the partial restore is rolled back. "
                    "name=", snapshot.name);
                // 역순 — 깊은 자식부터 — 이면 각 객체가 지워지는 순간까지 살아 있다.
                for (auto created = createdIds.rbegin(); created != createdIds.rend(); ++created)
                {
                    static_cast<void>(scene->RemoveGameObject(*created));
                }
                return false;
            }
            createdIds.push_back(gameObject->GetInstanceId());
            gameObject->SetActive(snapshot.isActive);

            for (const auto& [oldComponentId, componentSnapshot] : snapshot.components)
            {
                // Transform도 같은 길로 되세운다: 객체의 일부라 새로 붙지 않고 이미 있는 것이
                // 채워지며, 복원은 그 Transform을 돌려준다.
                Component* const restored = RestoreComponent(componentSnapshot, *gameObject);
                if (!restored)
                {
                    // 이 컴포넌트의 옛 id는 별칭을 얻지 못한다: 그 id를 잡은 커맨드는 영구
                    // 미해석이고, 실행되는 순간 해석 실패 로그가 말한다.
                    GameEngine::Diagnostics::Debug::LogWarning(
                        "Undo dropped a component it could not restore. type=",
                        componentSnapshot.typeName, ", oldId=", oldComponentId);
                    continue;
                }
                aliases.emplace_back(oldComponentId, restored->GetInstanceId());
            }

            if (snapshot.parentId != 0)
            {
                GameEngine::Runtime::Transform* parentTransform = nullptr;
                if (const auto restored = restoredByOldId.find(snapshot.parentId);
                    restored != restoredByOldId.end())
                {
                    parentTransform = &restored->second->GetTransform();
                }
                else if (GameObject* const parent = host.FindGameObject( snapshot.parentId))
                {
                    parentTransform = &parent->GetTransform();
                }
                // 로컬 값은 위에서 스냅숏대로 세웠으므로 월드 유지 재계산 없이 붙인다.
                if (!parentTransform ||
                    !gameObject->GetTransform().SetParent(parentTransform, false))
                {
                    GameEngine::Diagnostics::Debug::LogWarning(
                        "Undo restored a GameObject at the scene root because its parent could "
                        "not be resolved. name=", snapshot.name);
                }
            }

            gameObject->GetTransform().SetSiblingIndex(snapshot.siblingIndex);
            restoredByOldId.emplace(snapshot.id, gameObject);
            aliases.emplace_back(snapshot.id, gameObject->GetInstanceId());
        }

        // 전부 섰다. 이제부터 옛 id의 해석이 새 객체에 닿는다.
        for (const auto& [oldId, newId] : aliases)
        {
            host.RecordObjectIdAlias(oldId, newId);
        }
        return true;
    }
}

ComponentSnapshot SnapshotComponent(const Component& component)
{
    ComponentSnapshot snapshot;
    if (const auto* preserved =
            dynamic_cast<const GameEngine::Serialization::PreservedComponent*>(&component))
    {
        snapshot.typeName = preserved->GetPreservedTypeName();
    }
    else
    {
        snapshot.typeName = std::string(component.GetComponentType().GetName());
    }

    // 저장이 쓰는 것과 같은 JSON이 곧 스냅숏이다. 직렬화가 아는 것과 undo가 되돌리는 것이
    // 같아야 한다는 계약을, 두 경로가 같은 코드를 통과하게 해서 지킨다.
    if (std::optional<GameEngine::Core::Json> json =
            GameEngine::Serialization::SceneSerializer::SaveComponentToJson(component))
    {
        snapshot.json = std::move(*json);
        snapshot.valid = true;
    }
    else
    {
        GameEngine::Diagnostics::Debug::LogWarning(
            "A component the engine cannot serialize will not be restorable by undo. type=",
            snapshot.typeName);
    }
    return snapshot;
}

// ---- PropertyEditCommand

PropertyEditCommand::PropertyEditCommand(
    IEditorObjectHost& host, const unsigned int componentId, std::string propertyName,
    PropertyValue before, PropertyValue after, const std::uint64_t mergeKey)
    : mHost(&host)
    , mComponentId(componentId)
    , mPropertyName(std::move(propertyName))
    , mBefore(std::move(before))
    , mAfter(std::move(after))
    , mMergeKey(mergeKey)
{
}

bool PropertyEditCommand::SetValue(const PropertyValue& value) const
{
    Component* const component = mHost->FindComponent( mComponentId);
    if (!component)
    {
        LogUnresolvedTarget("PropertyEdit", mComponentId);
        return false;
    }
    const PropertyDescriptor* const descriptor =
        GameEngine::Runtime::FindProperty(component->GetComponentType(), mPropertyName);
    if (!descriptor || !descriptor->TrySet(*component, value))
    {
        GameEngine::Diagnostics::Debug::LogWarning(
            "An undo command's property no longer applies; the command is dropped. property=",
            mPropertyName, ", id=", mComponentId);
        return false;
    }
    return true;
}

bool PropertyEditCommand::Apply()
{
    return SetValue(mAfter);
}

bool PropertyEditCommand::Revert()
{
    return SetValue(mBefore);
}

bool PropertyEditCommand::TryMerge(const GameEngine::Core::IEditCommand& next)
{
    const auto* const other = dynamic_cast<const PropertyEditCommand*>(&next);
    if (!other || mMergeKey == 0 || other->mMergeKey != mMergeKey ||
        other->mComponentId != mComponentId || other->mPropertyName != mPropertyName)
    {
        return false;
    }
    mAfter = other->mAfter;
    return true;
}

// ---- GameObjectNameCommand

GameObjectNameCommand::GameObjectNameCommand(
    IEditorObjectHost& host, const unsigned int gameObjectId, std::string before, std::string after,
    const std::uint64_t mergeKey)
    : mHost(&host)
    , mGameObjectId(gameObjectId)
    , mBefore(std::move(before))
    , mAfter(std::move(after))
    , mMergeKey(mergeKey)
{
}

bool GameObjectNameCommand::SetName(const std::string& name) const
{
    GameObject* const gameObject = mHost->FindGameObject( mGameObjectId);
    if (!gameObject)
    {
        LogUnresolvedTarget("GameObjectName", mGameObjectId);
        return false;
    }
    gameObject->SetName(name);
    return true;
}

bool GameObjectNameCommand::Apply()
{
    return SetName(mAfter);
}

bool GameObjectNameCommand::Revert()
{
    return SetName(mBefore);
}

bool GameObjectNameCommand::TryMerge(const GameEngine::Core::IEditCommand& next)
{
    const auto* const other = dynamic_cast<const GameObjectNameCommand*>(&next);
    if (!other || mMergeKey == 0 || other->mMergeKey != mMergeKey ||
        other->mGameObjectId != mGameObjectId)
    {
        return false;
    }
    mAfter = other->mAfter;
    return true;
}

// ---- GameObjectActiveCommand

GameObjectActiveCommand::GameObjectActiveCommand(
    IEditorObjectHost& host, const unsigned int gameObjectId, const bool after)
    : mHost(&host), mGameObjectId(gameObjectId), mAfter(after)
{
}

bool GameObjectActiveCommand::SetActive(const bool isActive) const
{
    GameObject* const gameObject = mHost->FindGameObject( mGameObjectId);
    if (!gameObject)
    {
        LogUnresolvedTarget("GameObjectActive", mGameObjectId);
        return false;
    }
    gameObject->SetActive(isActive);
    return true;
}

bool GameObjectActiveCommand::Apply()
{
    return SetActive(mAfter);
}

bool GameObjectActiveCommand::Revert()
{
    return SetActive(!mAfter);
}

// ---- AddComponentCommand

namespace
{
    /// <summary>타입 이름만 실은 컴포넌트 JSON이다. 로더가 나머지를 기본값으로 채운다.</summary>
    [[nodiscard]] GameEngine::Core::Json MakeTypeOnlyPrototype(const std::string& typeName)
    {
        GameEngine::Core::Json::Object object;
        object.emplace("type", GameEngine::Core::Json(typeName));
        return GameEngine::Core::Json(std::move(object));
    }
}

AddComponentCommand::AddComponentCommand(
    IEditorObjectHost& host, const unsigned int gameObjectId, std::string typeName)
    : AddComponentCommand(host, gameObjectId, typeName, MakeTypeOnlyPrototype(typeName))
{
}

AddComponentCommand::AddComponentCommand(
    IEditorObjectHost& host, const unsigned int gameObjectId, std::string typeName,
    GameEngine::Core::Json prototype)
    : mHost(&host)
    , mGameObjectId(gameObjectId)
    , mTypeName(std::move(typeName))
    , mPrototype(std::move(prototype))
{
}

namespace
{
    /// <summary>이름으로 등록된 컴포넌트 타입을 찾는다. 모르는 이름이면 null이다.</summary>
    [[nodiscard]] const GameEngine::Runtime::ComponentType* FindRegisteredType(
        const std::string& typeName)
    {
        for (const GameEngine::Runtime::ComponentType* const type :
            GameEngine::Serialization::RegisteredComponentTypes())
        {
            if (type != nullptr && type->GetName() == typeName)
            {
                return type;
            }
        }
        return nullptr;
    }
}

bool AddComponentCommand::Apply()
{
    GameObject* const gameObject = mHost->FindGameObject( mGameObjectId);
    if (!gameObject)
    {
        LogUnresolvedTarget("AddComponent", mGameObjectId);
        return false;
    }

    // 함께 있어야 하는 것을 먼저 붙인다. 사각형 없는 Button은 히트 테스트 후보에서 아예 빠져
    // 소리 없이 죽으므로, 사람이 그 사실을 모른 채 "안 눌리는 버튼"을 만들게 두지 않는다.
    // 먼저인 이유는 순간이라도 요구가 빠진 상태로 서지 않게 하기 위해서다.
    std::vector<unsigned int> addedRequirements;
    if (const GameEngine::Runtime::ComponentType* const type = FindRegisteredType(mTypeName))
    {
        for (const GameEngine::Runtime::ComponentType* const required :
            type->CollectRequiredComponents())
        {
            // 이미 있으면 두 번 붙이지 않는다. 사람이 손으로 먼저 붙였을 수 있다.
            bool alreadyThere = required == nullptr;
            for (const Component* const one :
                gameObject->GetComponents<GameEngine::Runtime::Component>())
            {
                if (one != nullptr && required != nullptr &&
                    one->GetComponentType().IsDerivedFrom(*required))
                {
                    alreadyThere = true;
                }
            }
            if (alreadyThere)
            {
                continue;
            }
            Component* const attached =
                GameEngine::Serialization::SceneSerializer::LoadComponentIntoGameObject(
                    MakeTypeOnlyPrototype(std::string(required->GetName())), *gameObject);
            if (!attached)
            {
                continue;
            }
            addedRequirements.push_back(attached->GetInstanceId());
            GameEngine::Diagnostics::Debug::Log(
                "Added a component that ", mTypeName, " requires. type=", required->GetName());
        }
    }

    // 장면 로드가 컴포넌트를 붙이는 그 길이다: 만들 줄 아는 타입은 그 타입이 되고, 모르는
    // 타입은 실린 JSON을 쥔 보존 컴포넌트가 된다.
    Component* const added =
        GameEngine::Serialization::SceneSerializer::LoadComponentIntoGameObject(
            mPrototype, *gameObject);
    if (!added)
    {
        // 본체가 서지 못했으면 그것 때문에 붙인 것도 남기지 않는다.
        for (auto id = addedRequirements.rbegin(); id != addedRequirements.rend(); ++id)
        {
            if (Component* const one = mHost->FindComponent( *id))
            {
                static_cast<void>(gameObject->RemoveComponent(one));
            }
        }
        return false;
    }
    const unsigned int newId = added->GetInstanceId();
    if (mComponentId == 0)
    {
        // 첫 실행: 이 id가 이 컴포넌트의 이름이 된다. 추가 뒤의 속성 편집들이 이것을 잡는다.
        mComponentId = newId;
        mRequirementIds = std::move(addedRequirements);
    }
    else
    {
        // 재실행: 새로 만들어진 컴포넌트를 처음 이름의 별칭으로 잇는다. 함께 붙인 것들도
        // 같은 이유로 이어야 되돌리기가 그것들을 다시 찾는다.
        mHost->RecordObjectIdAlias(mComponentId, newId);
        for (std::size_t index = 0;
            index < mRequirementIds.size() && index < addedRequirements.size(); ++index)
        {
            mHost->RecordObjectIdAlias(mRequirementIds[index], addedRequirements[index]);
        }
    }
    return true;
}

bool AddComponentCommand::Revert()
{
    Component* const component = mHost->FindComponent( mComponentId);
    if (!component || !component->GetGameObject())
    {
        LogUnresolvedTarget("AddComponent", mComponentId);
        return false;
    }
    GameObject* const gameObject = component->GetGameObject();
    if (!gameObject->RemoveComponent(component))
    {
        return false;
    }
    // 함께 붙인 것도 함께 걷는다. 되돌리기가 절반만 되돌리면 사람이 놓지 않은 컴포넌트가
    // 남는다. 붙인 역순인 이유는 나중 것이 앞의 것에 기댈 수 있어서다.
    for (auto id = mRequirementIds.rbegin(); id != mRequirementIds.rend(); ++id)
    {
        if (Component* const one = mHost->FindComponent( *id))
        {
            static_cast<void>(gameObject->RemoveComponent(one));
        }
    }
    return true;
}

// ---- PreservedPropertyEditCommand

PreservedPropertyEditCommand::PreservedPropertyEditCommand(
    IEditorObjectHost& host, const unsigned int componentId, std::string memberName,
    GameEngine::Core::Json before, GameEngine::Core::Json after, const std::uint64_t mergeKey)
    : mHost(&host)
    , mComponentId(componentId)
    , mMemberName(std::move(memberName))
    , mBefore(std::move(before))
    , mAfter(std::move(after))
    , mMergeKey(mergeKey)
{
}

bool PreservedPropertyEditCommand::Apply()
{
    return SetValue(mAfter);
}

bool PreservedPropertyEditCommand::Revert()
{
    return SetValue(mBefore);
}

bool PreservedPropertyEditCommand::TryMerge(const GameEngine::Core::IEditCommand& next)
{
    const auto* const edit = dynamic_cast<const PreservedPropertyEditCommand*>(&next);
    if (!edit || mMergeKey == 0 || edit->mMergeKey != mMergeKey ||
        edit->mComponentId != mComponentId || edit->mMemberName != mMemberName)
    {
        return false;
    }
    // 흡수하는 쪽은 자기 "이전 값"을 지킨다: 타이핑 한 줄이 시작 전 값으로 한 번에 돌아간다.
    mAfter = edit->mAfter;
    return true;
}

bool PreservedPropertyEditCommand::SetValue(const GameEngine::Core::Json& value) const
{
    Component* const component = mHost->FindComponent( mComponentId);
    auto* const preserved =
        dynamic_cast<GameEngine::Serialization::PreservedComponent*>(component);
    if (!preserved)
    {
        LogUnresolvedTarget("PreservedPropertyEdit", mComponentId);
        return false;
    }
    GameEngine::Core::Json::Object members =
        preserved->GetData().IsObject() ? preserved->GetData().AsObject()
                                        : GameEngine::Core::Json::Object{};
    members.insert_or_assign(mMemberName, value);
    preserved->SetData(GameEngine::Core::Json(std::move(members)));
    return true;
}


// ---- RemoveComponentCommand

RemoveComponentCommand::RemoveComponentCommand(IEditorObjectHost& host, const Component& component)
    : mHost(&host)
    , mGameObjectId(component.GetGameObject() ? component.GetGameObject()->GetInstanceId() : 0)
    , mComponentId(component.GetInstanceId())
    , mSnapshot(SnapshotComponent(component))
{
}

bool RemoveComponentCommand::Apply()
{
    Component* const component = mHost->FindComponent( mComponentId);
    if (!component || !component->GetGameObject())
    {
        LogUnresolvedTarget("RemoveComponent", mComponentId);
        return false;
    }
    return component->GetGameObject()->RemoveComponent(component);
}

bool RemoveComponentCommand::Revert()
{
    GameObject* const gameObject = mHost->FindGameObject( mGameObjectId);
    if (!gameObject)
    {
        LogUnresolvedTarget("RemoveComponent", mGameObjectId);
        return false;
    }
    Component* const added = RestoreComponent(mSnapshot, *gameObject);
    if (!added)
    {
        GameEngine::Diagnostics::Debug::LogWarning(
            "Undo could not reattach a removed component; the command is dropped. type=",
            mSnapshot.typeName, ", id=", mComponentId);
        return false;
    }
    mHost->RecordObjectIdAlias(mComponentId, added->GetInstanceId());
    return true;
}

// ---- CreateGameObjectCommand

CreateGameObjectCommand::CreateGameObjectCommand(IEditorObjectHost& host, std::string name)
    : mHost(&host), mName(std::move(name))
{
}

bool CreateGameObjectCommand::Apply()
{
    GameEngine::Runtime::Scene* const scene = mHost->GetOpenScene();
    GameObject* const gameObject = scene ? scene->CreateGameObject(mName) : nullptr;
    if (!gameObject)
    {
        return false;
    }
    const unsigned int newId = gameObject->GetInstanceId();
    const unsigned int newTransformId = gameObject->GetTransform().GetInstanceId();
    if (mGameObjectId == 0)
    {
        mGameObjectId = newId;
        mTransformId = newTransformId;
    }
    else
    {
        mHost->RecordObjectIdAlias(mGameObjectId, newId);
        mHost->RecordObjectIdAlias(mTransformId, newTransformId);
    }
    // 선택은 이 편집의 일부다: 처음 만들 때도, redo로 되살릴 때도 만든 것이 선택된다.
    mHost->SelectObject(newId);
    return true;
}

bool CreateGameObjectCommand::Revert()
{
    GameObject* const gameObject = mHost->FindGameObject( mGameObjectId);
    GameEngine::Runtime::Scene* const scene = gameObject ? gameObject->GetScene() : nullptr;
    if (!scene)
    {
        LogUnresolvedTarget("CreateGameObject", mGameObjectId);
        return false;
    }
    // 이 커맨드는 빈 객체를 만들었다. 지금 자식이 있다면 기록되지 않은 경로로 생긴 것이고 —
    // 기록된 자식은 LIFO상 이 undo보다 먼저 되돌려졌다 — 스냅숏 없이 지우면 복구 불능이므로,
    // 되돌리기를 거부하고 로그로 말한다.
    if (!gameObject->GetTransform().GetChildren().empty())
    {
        GameEngine::Diagnostics::Debug::LogWarning(
            "Undo refused to delete a created GameObject because it has children that were not "
            "recorded; the command is dropped. id=", mGameObjectId);
        return false;
    }
    return scene->RemoveGameObject(gameObject->GetInstanceId());
}

// ---- DeleteGameObjectCommand

DeleteGameObjectCommand::DeleteGameObjectCommand(IEditorObjectHost& host, GameObject& gameObject)
    : mHost(&host), mObjects(SnapshotSubtree(gameObject))
{
}

bool DeleteGameObjectCommand::Apply()
{
    return DeleteSubtree(*mHost, mObjects, "DeleteGameObject");
}

bool DeleteGameObjectCommand::Revert()
{
    return RestoreSubtree(*mHost, mObjects);
}

// ---- InstantiateModelCommand

InstantiateModelCommand::InstantiateModelCommand(IEditorObjectHost& host, GameObject& gameObject)
    : mHost(&host), mObjects(SnapshotSubtree(gameObject))
{
}

bool InstantiateModelCommand::Apply()
{
    if (!RestoreSubtree(*mHost, mObjects))
    {
        return false;
    }
    // 브라우저의 첫 인스턴스화가 그랬듯, 되살린 뿌리가 선택된다.
    if (const GameObject* const root = mHost->FindGameObject( mObjects.front().id))
    {
        mHost->SelectObject(root->GetInstanceId());
    }
    return true;
}

bool InstantiateModelCommand::Revert()
{
    return DeleteSubtree(*mHost, mObjects, "InstantiateModel");
}

// ---- ReparentGameObjectCommand

ReparentGameObjectCommand::ReparentGameObjectCommand(
    IEditorObjectHost& host, const unsigned int gameObjectId,
    const unsigned int oldParentId, const TransformState& oldLocal,
    const unsigned int newParentId, const TransformState& newLocal)
    : mHost(&host)
    , mGameObjectId(gameObjectId)
    , mOldParentId(oldParentId)
    , mNewParentId(newParentId)
    , mOldLocal(oldLocal)
    , mNewLocal(newLocal)
{
}

bool ReparentGameObjectCommand::Reparent(
    const unsigned int parentId, const TransformState& local) const
{
    GameObject* const gameObject = mHost->FindGameObject( mGameObjectId);
    if (!gameObject)
    {
        LogUnresolvedTarget("Reparent", mGameObjectId);
        return false;
    }
    GameEngine::Runtime::Transform* parentTransform = nullptr;
    if (parentId != 0)
    {
        GameObject* const parent = mHost->FindGameObject( parentId);
        if (!parent)
        {
            LogUnresolvedTarget("Reparent", parentId);
            return false;
        }
        parentTransform = &parent->GetTransform();
    }
    if (!gameObject->GetTransform().SetParent(parentTransform, false))
    {
        return false;
    }
    // 기록된 로컬 값을 그대로 되세운다. 월드 유지 재계산을 다시 돌리면 부동소수가 흔들린다.
    gameObject->GetTransform().SetPosition(local.position);
    gameObject->GetTransform().SetRotation(local.rotation);
    gameObject->GetTransform().SetScale(local.scale);
    gameObject->GetTransform().SetSiblingIndex(local.siblingIndex);
    return true;
}

bool ReparentGameObjectCommand::Apply()
{
    return Reparent(mNewParentId, mNewLocal);
}

bool ReparentGameObjectCommand::Revert()
{
    return Reparent(mOldParentId, mOldLocal);
}

}
