#include "EditorSiblingUndoTests.h"

#include <limits>
#include <string>

#include "Document/EditorCommands.h"
#include "Document/EditorUndoService.h"
#include "Rules/EditorObjectHost.h"
#include "Runtime/Component.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/Transform.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "TestSupport.h"

namespace
{
    using namespace GameEngine::Runtime;

    class SiblingObjectHost final : public GameEditor::IEditorObjectHost
    {
    public:
        SiblingObjectHost(ObjectRegistry& registry, Scene& scene) : mRegistry(registry), mScene(scene) {}

        Component* FindComponent(const unsigned int id) override
        {
            return dynamic_cast<Component*>(mRegistry.FindObject(mUndo.ResolveObjectId(id)));
        }
        GameObject* FindGameObject(const unsigned int id) override
        {
            return dynamic_cast<GameObject*>(mRegistry.FindObject(mUndo.ResolveObjectId(id)));
        }
        void RecordObjectIdAlias(const unsigned int oldId, const unsigned int newId) override
        {
            mUndo.RecordObjectIdAlias(oldId, newId);
        }
        void SelectObject(unsigned int) override {}
        Scene* GetOpenScene() const override { return &mScene; }

    private:
        ObjectRegistry& mRegistry;
        Scene& mScene;
        GameEditor::EditorUndoService mUndo;
    };

    std::string ChildOrder(const GameObject& parent)
    {
        std::string order;
        for (const Transform* const child : parent.GetTransform().GetChildren())
        {
            if (!order.empty())
            {
                order += ",";
            }
            order += child->GetGameObject()->GetName();
        }
        return order;
    }

    GameEditor::ReparentGameObjectCommand::TransformState Capture(const Transform& transform)
    {
        return { transform.GetPosition(), transform.GetRotation(), transform.GetScale(),
            transform.GetSiblingIndex() };
    }
}

bool RunEditorSiblingUndoTests()
{
    using TestSupport::Expect;
    static_cast<void>(GameEngine::Serialization::RegisterRuntimeComponentFactories());
    ObjectRegistry registry;
    Input input;
    RuntimeContext context(registry, input);
    Scene scene(context);
    SiblingObjectHost host(registry, scene);
    auto* const parent = scene.CreateGameObject("Parent");
    auto* const a = scene.CreateGameObject("A");
    auto* b = scene.CreateGameObject("B");
    auto* const c = scene.CreateGameObject("C");
    auto* const x = scene.CreateGameObject("X");
    auto* const y = scene.CreateGameObject("Y");
    auto* const other = scene.CreateGameObject("Other");
    auto* const d = scene.CreateGameObject("D");
    auto* const e = scene.CreateGameObject("E");
    if (!Expect(parent && a && b && c && x && y && other && d && e,
        "the sibling test hierarchy should be created"))
    {
        return false;
    }
    if (!Expect(a->GetTransform().SetParent(&parent->GetTransform()) &&
        b->GetTransform().SetParent(&parent->GetTransform()) &&
        c->GetTransform().SetParent(&parent->GetTransform()) &&
        x->GetTransform().SetParent(&b->GetTransform()) &&
        y->GetTransform().SetParent(&b->GetTransform()) &&
        d->GetTransform().SetParent(&other->GetTransform()) &&
        e->GetTransform().SetParent(&other->GetTransform()), "the sibling hierarchy should attach"))
    {
        return false;
    }
    x->GetTransform().SetAsLastSibling();
    const unsigned int bId = b->GetInstanceId();
    GameEditor::DeleteGameObjectCommand deletion(host, *b);
    bool passed = true;
    for (int pass = 0; pass < 2; ++pass)
    {
        passed &= Expect(deletion.Apply() && ChildOrder(*parent) == "A,C",
            "deleting the middle subtree should preserve its surviving siblings");
        if (!Expect(deletion.Revert(), "the deleted subtree should restore"))
        {
            return false;
        }
        b = host.FindGameObject(bId);
        passed &= Expect(b && ChildOrder(*parent) == "A,B,C" && ChildOrder(*b) == "Y,X",
            "delete undo must restore both the external sibling slot and nested sibling order");
    }
    if (!b)
    {
        return false;
    }

    b->GetTransform().SetPosition({ 3.0f, 4.0f, 5.0f });
    other->GetTransform().SetPosition({ 10.0f, 20.0f, 30.0f });
    const auto oldLocal = Capture(b->GetTransform());
    if (!Expect(b->GetTransform().SetParent(&other->GetTransform(), true),
        "the child should move to the second parent"))
    {
        return false;
    }
    b->GetTransform().SetSiblingIndex(1);
    const auto newLocal = Capture(b->GetTransform());
    GameEditor::ReparentGameObjectCommand reparent(host, bId,
        parent->GetInstanceId(), oldLocal, other->GetInstanceId(), newLocal);
    for (int pass = 0; pass < 2; ++pass)
    {
        passed &= Expect(reparent.Revert() && ChildOrder(*parent) == "A,B,C" &&
            ChildOrder(*other) == "D,E" && b->GetTransform().GetPosition() == oldLocal.position,
            "reparent undo should restore the old sibling slot and exact local transform");
        passed &= Expect(reparent.Apply() && ChildOrder(*parent) == "A,C" &&
            ChildOrder(*other) == "D,B,E" && b->GetTransform().GetPosition() == newLocal.position,
            "reparent redo should restore the new sibling slot and exact local transform");
    }
    passed &= Expect(reparent.Revert(), "the reparent should return to its original parent");
    a->GetTransform().SetSiblingIndex(std::numeric_limits<std::size_t>::max());
    passed &= Expect(ChildOrder(*parent) == "B,C,A" && a->GetTransform().GetSiblingIndex() == 2,
        "out-of-range sibling positions should clamp to the last slot");
    a->GetTransform().SetSiblingIndex(0);
    parent->GetTransform().SetSiblingIndex(100);
    passed &= Expect(ChildOrder(*parent) == "A,B,C" && parent->GetTransform().GetSiblingIndex() == 0,
        "sibling movement should also support the first slot and leave scene roots unchanged");
    return passed;
}

static const TestSupport::Registration gEditorSiblingUndo{
    "EditorDocument", "Delete and reparent undo preserve external and nested sibling order",
    RunEditorSiblingUndoTests };
