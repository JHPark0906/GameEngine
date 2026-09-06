#include <iostream>
#include <memory>
#include <string>
#include <algorithm>
#include <functional>
#include <ranges>
#include <unordered_map>
#include <vector>

#include "../GameEditor/Source/Document/EditorCommands.h"
#include "../GameEditor/Source/Rules/EditorObjectHost.h"
#include "../GameEngine/Math/Vector.h"
#include "../GameEngine/Runtime/Component.h"
#include "../GameEngine/Runtime/GameObject.h"
#include "../GameEngine/Runtime/Input.h"
#include "../GameEngine/Runtime/ObjectRegistry.h"
#include "../GameEngine/Runtime/RuntimeContext.h"
#include "../GameEngine/Runtime/Scene.h"
#include "../GameEngine/Runtime/SpriteRenderer.h"
#include "../GameEngine/Runtime/Transform.h"
#include "../GameEngine/Core/Json.h"
#include "../GameEngine/Serialization/PreservedComponent.h"
#include "../GameEngine/Serialization/RuntimeComponentFactories.h"

#include "EditCommandUndoTests.h"
#include "TestSupport.h"

using GameEditor::IEditorObjectHost;
using GameEngine::Runtime::GameObject;
using GameEngine::Runtime::Scene;
using TestSupport::Expect;

namespace
{

/// <summary>
/// 명령이 편집기에게 묻는 넷만 답하는 호스트다. 장면 하나와 별칭 표뿐이며, 프로젝트도 문서도
/// 없다 — 명령이 실제로 그 넷 말고는 아무것도 필요로 하지 않는다는 것이 이 시험의 전제이자
/// 결론이다.
/// </summary>
class SceneObjectHost final : public IEditorObjectHost
{
public:
    SceneObjectHost(GameEngine::Runtime::ObjectRegistry& registry, Scene& scene)
        : mRegistry(&registry), mScene(&scene)
    {
    }

    /// <summary>계층에서 고른 오브젝트다. 명령이 옮겨 놓는 것을 시험이 읽는다.</summary>
    unsigned int selected = 0;

    [[nodiscard]] GameEngine::Runtime::Component* FindComponent(
        const unsigned int instanceId) override
    {
        return dynamic_cast<GameEngine::Runtime::Component*>(Find(instanceId));
    }

    [[nodiscard]] GameObject* FindGameObject(const unsigned int instanceId) override
    {
        return dynamic_cast<GameObject*>(Find(instanceId));
    }

    void RecordObjectIdAlias(const unsigned int oldId, const unsigned int newId) override
    {
        const unsigned int from = Resolve(oldId);
        if (from != newId)
        {
            mAliases[from] = newId;
        }
    }

    void SelectObject(const unsigned int instanceId) override { selected = instanceId; }

    [[nodiscard]] Scene* GetOpenScene() const override { return mScene; }

private:
    /// <summary>별칭 사슬을 끝까지 따라간다. 편집기의 것과 같은 규칙이다.</summary>
    [[nodiscard]] unsigned int Resolve(const unsigned int instanceId) const
    {
        const auto alias = mAliases.find(instanceId);
        return alias == mAliases.end() ? instanceId : Resolve(alias->second);
    }

    [[nodiscard]] GameEngine::Runtime::Object* Find(const unsigned int instanceId) const
    {
        return instanceId == 0 ? nullptr : mRegistry->FindObject(Resolve(instanceId));
    }

    GameEngine::Runtime::ObjectRegistry* mRegistry;
    Scene* mScene;
    mutable std::unordered_map<unsigned int, unsigned int> mAliases;
};

/// <summary>적용 → 되돌리기 → 다시하기를 돌리고, 각 단계의 상태를 읽어 본다.</summary>
/// <param name="command">시험할 명령이다. 이미 만들어진 것을 받는다.</param>
/// <param name="read">지금 상태를 사람이 읽을 문자열로 만드는 것이다.</param>
/// <param name="what">붉어졌을 때 어느 명령인지 말할 이름이다.</param>
[[nodiscard]] bool RoundTrips(
    GameEditor::IEditCommand& command, const std::function<std::string()>& read,
    const std::string& what)
{
    const std::string before = read();
    bool passed = Expect(command.Apply(), (what + ": applying should succeed").c_str());
    const std::string applied = read();
    passed = Expect(applied != before, (what + ": applying should change something").c_str())
        && passed;

    passed = Expect(command.Revert(), (what + ": reverting should succeed").c_str()) && passed;
    passed = Expect(
        read() == before, (what + ": reverting should put the state back as it was").c_str())
        && passed;

    passed = Expect(command.Apply(), (what + ": redoing should succeed").c_str()) && passed;
    passed = Expect(
        read() == applied, (what + ": redoing should reach the same state as the first apply")
        .c_str()) && passed;
    return passed;
}

/// <summary>장면 전체를 한 문자열로 읽는다. 이름·활성·부모·컴포넌트 종류와 개수.</summary>
[[nodiscard]] std::string DescribeScene(const Scene& scene)
{
    std::vector<std::string> lines;
    for (const auto& object : scene.GetGameObjects() | std::views::values)
    {
        if (!object)
        {
            continue;
        }
        std::string line = object->GetName() + (object->IsActive() ? "|on" : "|off");
        const GameEngine::Runtime::Transform* const parent = object->GetTransform().GetParent();
        line += "|parent=" + (parent && parent->GetGameObject()
            ? parent->GetGameObject()->GetName() : std::string("none"));
        std::size_t components = 0;
        for (const auto& component : object->GetComponents<GameEngine::Runtime::Component>())
        {
            static_cast<void>(component);
            ++components;
        }
        line += "|components=" + std::to_string(components);
        lines.push_back(std::move(line));
    }
    std::ranges::sort(lines);
    std::string text;
    for (const std::string& line : lines)
    {
        text += line + "\n";
    }
    return text;
}

}

bool RunEditCommandUndoTests()
{
    std::cout << "running edit command undo tests\n";
    static_cast<void>(GameEngine::Serialization::RegisterRuntimeComponentFactories());

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    Scene scene(context, "Commands");
    SceneObjectHost host(registry, scene);

    GameObject* const target = scene.CreateGameObject("Target");
    if (!Expect(target != nullptr, "the test scene should hold an object"))
    {
        return false;
    }
    const unsigned int targetId = target->GetInstanceId();
    const auto describe = [&scene] { return DescribeScene(scene); };

    bool passed = true;

    // 이름을 바꾼다.
    {
        GameEditor::GameObjectNameCommand command(host, targetId, "Target", "Renamed", 0);
        passed = RoundTrips(command, describe, "renaming an object") && passed;
    }

    // 켜고 끈다.
    {
        GameEditor::GameObjectActiveCommand command(host, targetId, false);
        passed = RoundTrips(command, describe, "toggling an object") && passed;
    }

    // 컴포넌트를 붙인다.
    {
        GameEditor::AddComponentCommand command(host, targetId, "SpriteRenderer");
        passed = RoundTrips(command, describe, "adding a component") && passed;
    }

    // 속성을 바꾼다. 대상은 Transform의 위치다 — 장면만 있으면 서는 컴포넌트라
    // 에셋 없이 잴 수 있다.
    {
        const unsigned int transformId = target->GetTransform().GetInstanceId();
        GameEditor::PropertyEditCommand command(
            host, transformId, "position",
            GameEngine::Runtime::PropertyValue{ GameEngine::Math::Vector3{ 0.0f, 0.0f, 0.0f } },
            GameEngine::Runtime::PropertyValue{ GameEngine::Math::Vector3{ 3.0f, 4.0f, 5.0f } }, 0);
        const auto readPosition = [target]
        {
            const GameEngine::Math::Vector3 p = target->GetTransform().GetPosition();
            return std::to_string(p.GetX()) + "," + std::to_string(p.GetY()) + "," +
                std::to_string(p.GetZ());
        };
        passed = RoundTrips(command, readPosition, "editing a property") && passed;
    }

    // 컴포넌트를 뗀다. 붙였던 것을 대상으로 한다.
    {
        GameEngine::Runtime::SpriteRenderer* const renderer =
            target->GetComponent<GameEngine::Runtime::SpriteRenderer>();
        if (Expect(renderer != nullptr, "the added component should still be there"))
        {
            GameEditor::RemoveComponentCommand command(host, *renderer);
            passed = RoundTrips(command, describe, "removing a component") && passed;
        }
        else
        {
            passed = false;
        }
    }

    // 오브젝트를 만든다. 만든 것이 선택으로도 옮겨진다.
    {
        GameEditor::CreateGameObjectCommand command(host, "Made");
        passed = RoundTrips(command, describe, "creating an object") && passed;
        passed = Expect(
            host.selected != 0 && host.FindGameObject(host.selected) != nullptr,
            "creating an object should select something that exists") && passed;
    }

    // 부모를 바꾼다. 되돌리기가 부모와 지역 변환을 함께 되돌려야 한다.
    {
        GameObject* const parent = scene.CreateGameObject("Parent");
        if (!Expect(parent != nullptr, "the test scene should hold a parent"))
        {
            return false;
        }
        const GameEditor::ReparentGameObjectCommand::TransformState atRoot{
            { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } };
        const GameEditor::ReparentGameObjectCommand::TransformState underParent{
            { 1.0f, 2.0f, 3.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } };
        GameEditor::ReparentGameObjectCommand command(
            host, targetId, 0, atRoot, parent->GetInstanceId(), underParent);
        passed = RoundTrips(command, describe, "reparenting an object") && passed;
    }

    // 오브젝트를 지운다. 되돌리기가 그것을 다시 세우고, 새 id를 옛 id에 이어야 한다.
    {
        GameObject* const doomed = scene.CreateGameObject("Doomed");
        if (!Expect(doomed != nullptr, "the test scene should hold a doomed object"))
        {
            return false;
        }
        const unsigned int doomedId = doomed->GetInstanceId();
        GameEditor::DeleteGameObjectCommand command(host, *doomed);
        passed = RoundTrips(command, describe, "deleting an object") && passed;

        // 되살아난 것은 새 id를 갖는다. 별칭이 이어지지 않으면 옛 id로는 닿지 못하고, 그다음
        // 되돌리기가 조용히 아무 일도 하지 않는다 — 이 시험이 그 자리를 잡는 유일한 곳이다.
        passed = Expect(command.Revert(), "the delete should revert once more") && passed;
        passed = Expect(
            host.FindGameObject(doomedId) != nullptr,
            "the old id should reach the object that came back, through the alias") && passed;
    }

    // 보존된 컴포넌트의 멤버를 바꾼다. 이 프로세스가 모르는 컴포넌트를 장면이 들고 있을 때의
    // 길이며, 알 수 없는 것을 잃지 않고 편집하게 하는 자리다.
    {
        auto* const preserved =
            target->AddComponent<GameEngine::Serialization::PreservedComponent>();
        if (!Expect(preserved != nullptr, "the scene should take a preserved component"))
        {
            return false;
        }
        GameEngine::Core::Json::Object members;
        members.insert_or_assign("type", GameEngine::Core::Json(std::string("Unknown")));
        members.insert_or_assign("speed", GameEngine::Core::Json(1.0));
        preserved->SetData(GameEngine::Core::Json(std::move(members)));

        GameEditor::PreservedPropertyEditCommand command(
            host, preserved->GetInstanceId(), "speed", GameEngine::Core::Json(1.0),
            GameEngine::Core::Json(9.0), 0);
        const auto readData = [preserved] { return preserved->GetData().Dump(); };
        passed = RoundTrips(command, readData, "editing a preserved member") && passed;
    }

    // 모델 인스턴스화는 거꾸로 돈다. 명령이 만들어질 때 오브젝트는 이미 서 있고, 되돌리기가
    // 그것을 걷어 내며 다시하기가 되살린다. 그래서 Apply부터 도는 위의 왕복과 순서가 다르다.
    {
        GameObject* const model = scene.CreateGameObject("Model");
        if (!Expect(model != nullptr, "the test scene should hold a model root"))
        {
            return false;
        }
        const unsigned int modelId = model->GetInstanceId();
        GameEditor::InstantiateModelCommand command(host, *model);
        const std::string instantiated = describe();

        passed = Expect(command.Revert(), "undoing an instantiation should succeed") && passed;
        const std::string undone = describe();
        passed = Expect(
            undone != instantiated,
            "undoing an instantiation should take the object away") && passed;

        passed = Expect(command.Apply(), "redoing an instantiation should succeed") && passed;
        passed = Expect(
            describe() == instantiated,
            "redoing an instantiation should put the object back as it was") && passed;
        passed = Expect(
            host.FindGameObject(modelId) != nullptr,
            "the old id should reach the reinstantiated object, through the alias") && passed;
    }

    return passed;
}

static const TestSupport::Registration gEditCommandUndoTests{
    "EditorDocument", "edit command undo tests should pass", RunEditCommandUndoTests };
