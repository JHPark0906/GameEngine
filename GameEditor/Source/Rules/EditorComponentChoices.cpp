#include "Rules/EditorComponentChoices.h"

#include <algorithm>
#include <string>
#include <unordered_set>

#include "Runtime/ComponentType.h"
#include "Serialization/ComponentFactory.h"
#include "Serialization/ComponentSchema.h"
#include "Serialization/RuntimeComponentFactories.h"

namespace GameEditor
{

std::vector<EditorComponentChoice> BuildEditorComponentChoices(
    const std::span<const GameEngine::Serialization::ComponentSchema> schemas)
{
    using namespace GameEngine::Serialization;
    std::vector<EditorComponentChoice> choices;
    std::unordered_set<std::string> listed;
    std::unordered_set<std::string> nonCreatable;
    const auto engineTypes = EngineComponentTypes();
    for (const GameEngine::Runtime::ComponentType* const type : RegisteredComponentTypes())
    {
        const std::string name(type->GetName());
        if (!type->IsCreatable())
        {
            nonCreatable.insert(name);
            continue;
        }
        if (!ComponentFactory::IsRegistered(name) || !listed.insert(name).second)
        {
            continue;
        }
        const bool isEngineType = std::find(engineTypes.begin(), engineTypes.end(), type)
            != engineTypes.end();
        choices.push_back({ name, {}, !isEngineType });
    }
    for (const ComponentSchema& schema : schemas)
    {
        if (nonCreatable.contains(schema.typeName) || !listed.insert(schema.typeName).second)
        {
            continue;
        }
        choices.push_back({ schema.typeName, MakeDefaultComponentJson(schema), true });
    }
    return choices;
}

}
