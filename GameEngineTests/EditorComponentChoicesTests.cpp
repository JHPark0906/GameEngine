#include "EditorComponentChoicesTests.h"

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Core/Json.h"
#include "Rules/EditorComponentChoices.h"
#include "Runtime/Component.h"
#include "Runtime/ComponentType.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Serialization/ComponentFactory.h"
#include "Serialization/ComponentSchema.h"
#include "Serialization/PreservedComponent.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Serialization/SceneSerializer.h"
#include "TestSupport.h"

namespace
{
    class LinkedEditorChoiceProbe final : public GameEngine::Runtime::Component
    {
    public:
        static const GameEngine::Runtime::ComponentType& StaticType()
        {
            static const GameEngine::Runtime::ComponentType type{
                "LinkedEditorChoiceProbe", &Component::StaticType(), nullptr,
                &GameEngine::Runtime::MakeComponentInstance<LinkedEditorChoiceProbe> };
            return type;
        }
        const GameEngine::Runtime::ComponentType& GetComponentType() const override
        {
            return StaticType();
        }
    };
}

bool RunEditorComponentChoicesTests()
{
    using namespace GameEngine;
    using TestSupport::Expect;
    TestSupport::RegistryScope registryScope;
    static_cast<void>(Serialization::RegisterRuntimeComponentFactories());
    if (!Expect(Serialization::RegisterComponentType(LinkedEditorChoiceProbe::StaticType()),
        "the linked project component should register"))
    {
        return false;
    }
    const std::vector<Serialization::ComponentSchema> schemas{
        { "LinkedEditorChoiceProbe", {} },
        { "UnlinkedEditorChoiceProbe", {} },
        { "UnlinkedEditorChoiceProbe", {} },
        { "Transform", {} }
    };
    const auto choices = GameEditor::BuildEditorComponentChoices(schemas);
    const auto count = [&choices](const std::string& name)
    {
        return std::count_if(choices.begin(), choices.end(), [&name](const auto& choice)
        {
            return choice.typeName == name;
        });
    };
    bool passed = Expect(count("LinkedEditorChoiceProbe") == 1,
        "a linked project component should appear once even when its schema is also present");
    passed &= Expect(count("UnlinkedEditorChoiceProbe") == 1 && count("Transform") == 0 &&
        count("SpriteRenderer") == 1,
        "schema choices should be unique and preserve the engine creatability rules");

    Runtime::ObjectRegistry registry;
    Runtime::Input input;
    Runtime::RuntimeContext context(registry, input);
    Runtime::Scene scene(context);
    auto* const object = scene.CreateGameObject("Choices");
    if (!Expect(object != nullptr, "component choices should have a target object"))
    {
        return false;
    }
    for (const auto& choice : choices)
    {
        if (choice.typeName == "LinkedEditorChoiceProbe")
        {
            passed &= Expect(choice.isGameComponent && choice.prototype.IsNull(),
                "the linked choice should use the live factory's native defaults");
            const Core::Json prototype(Core::Json::Object{ { "type", Core::Json(choice.typeName) } });
            passed &= Expect(dynamic_cast<LinkedEditorChoiceProbe*>(
                Serialization::SceneSerializer::LoadComponentIntoGameObject(prototype, *object)) != nullptr,
                "choosing the registered type should create the actual game component");
        }
        else if (choice.typeName == "UnlinkedEditorChoiceProbe")
        {
            passed &= Expect(choice.isGameComponent && !choice.prototype.IsNull(),
                "an unlinked project choice should supply its schema prototype");
            passed &= Expect(dynamic_cast<Serialization::PreservedComponent*>(
                Serialization::SceneSerializer::LoadComponentIntoGameObject(choice.prototype, *object)) != nullptr,
                "an unlinked choice should remain available as preserved scene data");
        }
    }
    passed &= Expect(Serialization::ComponentFactory::Unregister("LinkedEditorChoiceProbe"),
        "the probe factory should unregister");
    const auto afterUnregister = GameEditor::BuildEditorComponentChoices({});
    passed &= Expect(std::none_of(afterUnregister.begin(), afterUnregister.end(), [](const auto& choice)
        { return choice.typeName == "LinkedEditorChoiceProbe"; }),
        "historical type registrations without a live factory must not advertise native creation");
    return passed;
}

static const TestSupport::Registration gEditorComponentChoices{
    "EditorDocument", "Add Component includes linked project types and deduplicates schema fallbacks",
    RunEditorComponentChoicesTests };
