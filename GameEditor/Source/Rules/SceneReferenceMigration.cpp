#include "Rules/SceneReferenceMigration.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <variant>

#include "Assets/AssetDatabase.h"
#include "Assets/AssetReference.h"
#include "Diagnostics/Debug.h"
#include "Runtime/Component.h"
#include "Runtime/GameObject.h"
#include "Runtime/PropertyDescriptor.h"
#include "Runtime/Scene.h"
#include "Core/Json.h"
#include "Serialization/ComponentSchema.h"
#include "Serialization/PreservedComponent.h"

namespace GameEditor
{

namespace
{
    namespace Assets = GameEngine::Assets;
    namespace Runtime = GameEngine::Runtime;

    /// <summary>장면이 담고 있는 컴포넌트를 하나씩 방문한다.</summary>
    template <typename TVisitor>
    void ForEachComponent(const Runtime::Scene& scene, TVisitor&& visit)
    {
        for (const auto& gameObject : scene.GetGameObjects() | std::views::values)
        {
            if (!gameObject)
            {
                continue;
            }
            for (const std::unique_ptr<Runtime::Component>& owned :
                gameObject->GetAllComponents())
            {
                if (owned)
                {
                    visit(*owned);
                }
            }
        }
    }

    /// <summary>
    /// 장면의 모든 컴포넌트의 모든 에셋 참조 속성을 한 번씩 방문한다.
    ///
    /// 세는 쪽과 바꾸는 쪽이 이 함수 하나를 공유한다. 둘이 각자 걸으면 규칙이 조금 달라지는
    /// 날이 오고, 그날 사람은 「12개를 바꾸겠다」는 질문에 승인하고 11개가 바뀐 것을 모른다.
    ///
    /// 걷는 것이 <c>GetGameObjects</c>인 이유는 <see cref="SceneSerializer::SaveToText"/>가
    /// 바로 그 표를 걷기 때문이다. 저장이 보는 것과 이관이 보는 것이 같아야, 승인한 수와 파일에
    /// 남는 수가 어긋나지 않는다.
    /// </summary>
    template <typename TVisitor>
    void ForEachAssetReference(const Runtime::Scene& scene, TVisitor&& visit)
    {
        ForEachComponent(
            scene,
            [&visit](Runtime::Component& component)
            {
                for (const Runtime::PropertyDescriptor* const property :
                    Runtime::CollectProperties(component.GetComponentType()))
                {
                    if (property->GetKind() != Runtime::PropertyKind::AssetReference)
                    {
                        continue;
                    }
                    const Runtime::PropertyValue value = property->Get(component);
                    if (const auto* const reference = std::get_if<Assets::AssetReference>(&value))
                    {
                        visit(component, *property, *reference);
                    }
                }
            });
    }

    namespace Serialization = GameEngine::Serialization;

    /// <summary>이 컴포넌트가 보존 데이터로 실려 온 것이면 그것이고, 아니면 null이다.</summary>
    [[nodiscard]] const Serialization::PreservedComponent* AsPreserved(
        const Runtime::Component& component)
    {
        return component.GetComponentType().IsDerivedFrom(
                   Serialization::PreservedComponent::StaticType())
            ? static_cast<const Serialization::PreservedComponent*>(&component)
            : nullptr;
    }

    [[nodiscard]] const Serialization::ComponentSchema* FindSchema(
        const std::span<const Serialization::ComponentSchema> schemas,
        const std::string& typeName)
    {
        const auto found = std::ranges::find_if(
            schemas,
            [&typeName](const Serialization::ComponentSchema& schema)
            {
                return schema.typeName == typeName;
            });
        return found == schemas.end() ? nullptr : &*found;
    }

    /// <summary>
    /// 보존된 컴포넌트가 쥐고 있는 JSON 안의 에셋 참조들을 방문한다.
    ///
    /// 에디터는 게임이 정의한 컴포넌트의 팩토리를 갖지 못하므로 그런 컴포넌트는 언제나 파싱된
    /// JSON 그대로 실려 온다. 스키마가 「어느 열쇠가 참조인가」를 답해 주므로, 팩토리 없이도 그
    /// 안을 세고 옮길 수 있다. 스키마가 없으면 답할 사람이 없어 아무것도 방문하지 않는다.
    /// </summary>
    template <typename TVisitor>
    void ForEachPreservedReference(
        const Serialization::PreservedComponent& component,
        const Serialization::ComponentSchema& schema, TVisitor&& visit)
    {
        const GameEngine::Core::Json& data = component.GetData();
        if (!data.IsObject())
        {
            return;
        }
        const GameEngine::Core::Json::Object& members = data.AsObject();
        for (const Serialization::ComponentSchemaProperty& property : schema.properties)
        {
            if (property.kind != Runtime::PropertyKind::AssetReference)
            {
                continue;
            }
            const auto member = members.find(property.name);
            if (member == members.end() || !member->second.IsString())
            {
                continue;
            }
            visit(
                property.name,
                Assets::AssetReference::Parse(member->second.Get<std::string>()));
        }
    }

    /// <summary>참조 하나를 훑기의 칸 하나로 옮긴다. 서술된 것과 보존된 것이 같은 규칙을 탄다.</summary>
    void Classify(
        SceneReferenceSurvey& survey, const Assets::AssetDatabase& database,
        const Assets::AssetReference& reference)
    {
        if (!reference.IsValid())
        {
            return;
        }
        if (reference.IsGuidReference())
        {
            ++survey.alreadyMigrated;
            return;
        }
        const Assets::Asset* const asset = database.FindAsset(reference);
        if (!asset)
        {
            ++survey.unresolved;
            return;
        }
        if (!asset->GetGuid().IsValid())
        {
            survey.assetsWithoutIdentity.push_back(asset->GetRelativePath());
            return;
        }
        ++survey.convertible;
    }
}

SceneReferenceSurvey SurveySceneReferences(
    const Runtime::Scene& scene, const Assets::AssetDatabase& database,
    const std::span<const Serialization::ComponentSchema> schemas)
{
    SceneReferenceSurvey survey;
    ForEachComponent(
        scene,
        [&survey, &database, schemas](Runtime::Component& component)
        {
            const Serialization::PreservedComponent* const preserved = AsPreserved(component);
            if (!preserved)
            {
                return;
            }
            const Serialization::ComponentSchema* const schema =
                FindSchema(schemas, preserved->GetPreservedTypeName());
            if (!schema)
            {
                // 팩토리도 스키마도 없다. 이 안에 참조가 있는지조차 말할 수 없는 유일한 경우다.
                ++survey.opaqueComponents;
                if (std::ranges::find(
                        survey.opaqueTypeNames, preserved->GetPreservedTypeName()) ==
                    survey.opaqueTypeNames.end())
                {
                    survey.opaqueTypeNames.push_back(preserved->GetPreservedTypeName());
                }
                return;
            }
            ForEachPreservedReference(
                *preserved, *schema,
                [&survey, &database](const std::string&, const Assets::AssetReference& reference)
                {
                    Classify(survey, database, reference);
                });
        });
    ForEachAssetReference(
        scene,
        [&survey, &database](
            const Runtime::Component&, const Runtime::PropertyDescriptor&,
            const Assets::AssetReference& reference)
        {
            Classify(survey, database, reference);
        });
    return survey;
}

namespace
{
    /// <summary>
    /// 이 참조가 옮겨 갈 정체성이다. 옮길 수 없으면 값이 없다 — 가리키는 것이 없거나, 그 에셋에
    /// 아직 정체성이 없거나, 이미 정체성으로 적혀 있는 경우다.
    /// </summary>
    [[nodiscard]] std::optional<Assets::AssetReference> Identify(
        const Assets::AssetDatabase& database, const Assets::AssetReference& reference)
    {
        if (!reference.IsValid() || reference.IsGuidReference())
        {
            return std::nullopt;
        }
        const Assets::Asset* const asset = database.FindAsset(reference);
        if (!asset || !asset->GetGuid().IsValid())
        {
            return std::nullopt;
        }
        return Assets::AssetReference(asset->GetGuid(), reference.GetLocalId());
    }
}

std::size_t MigrateSceneReferences(
    Runtime::Scene& scene, const Assets::AssetDatabase& database,
    const std::span<const Serialization::ComponentSchema> schemas)
{
    std::size_t migrated = 0;
    // 보존된 컴포넌트부터. 쥐고 있는 JSON을 고쳐 다시 쥐여 주는 것이 이쪽의 「값 쓰기」다.
    ForEachComponent(
        scene,
        [&migrated, &database, schemas](Runtime::Component& component)
        {
            const Serialization::PreservedComponent* const preserved = AsPreserved(component);
            if (!preserved)
            {
                return;
            }
            const Serialization::ComponentSchema* const schema =
                FindSchema(schemas, preserved->GetPreservedTypeName());
            if (!schema || !preserved->GetData().IsObject())
            {
                return;
            }
            GameEngine::Core::Json::Object members = preserved->GetData().AsObject();
            std::size_t changed = 0;
            ForEachPreservedReference(
                *preserved, *schema,
                [&](const std::string& name, const Assets::AssetReference& reference)
                {
                    const std::optional<Assets::AssetReference> identified =
                        Identify(database, reference);
                    if (!identified)
                    {
                        return;
                    }
                    members.insert_or_assign(
                        name, GameEngine::Core::Json(identified->ToString()));
                    GameEngine::Diagnostics::Debug::Log(
                        "Reference migrated to an identity. component=",
                        preserved->GetPreservedTypeName(), ", property=", name,
                        ", path=", reference.ToString(), ", guid=", identified->ToString());
                    ++changed;
                });
            if (changed > 0)
            {
                static_cast<Serialization::PreservedComponent&>(component).SetData(
                    GameEngine::Core::Json(std::move(members)));
                migrated += changed;
            }
        });

    ForEachAssetReference(
        scene,
        [&migrated, &database](
            Runtime::Component& component, const Runtime::PropertyDescriptor& property,
            const Assets::AssetReference& reference)
        {
            const std::optional<Assets::AssetReference> identified =
                Identify(database, reference);
            if (!identified)
            {
                return;
            }
            if (!property.TrySet(component, Runtime::PropertyValue{ *identified }))
            {
                GameEngine::Diagnostics::Debug::LogWarning(
                    "A reference could not be rewritten as an identity and stays a path. "
                    "component=", component.GetComponentType().GetName(),
                    ", property=", property.GetName());
                return;
            }
            GameEngine::Diagnostics::Debug::Log(
                "Reference migrated to an identity. component=",
                component.GetComponentType().GetName(), ", property=", property.GetName(),
                ", path=", reference.ToString(), ", guid=", identified->ToString());
            ++migrated;
        });
    return migrated;
}

std::string SceneMigrationPlan::Describe() const
{
    if (IsBlocked())
    {
        // 막힌 이유부터 말한다. 무엇을 고쳐야 하는지가 사람이 지금 필요한 유일한 정보다.
        std::string text = "Cannot migrate yet.";
        if (!assetsWithoutIdentity.empty())
        {
            text += " " + std::to_string(assetsWithoutIdentity.size()) +
                " referenced assets have no identity, starting with " +
                assetsWithoutIdentity.front().generic_string() + ".";
        }
        if (opaqueComponents > 0)
        {
            text += " " + std::to_string(opaqueComponents) +
                " components have no schema, so their references cannot be seen (";
            for (std::size_t index = 0; index < opaqueTypeNames.size(); ++index)
            {
                text += (index > 0 ? ", " : "") + opaqueTypeNames[index];
            }
            text += "). Build the game project once to write the schema.";
        }
        return text;
    }

    std::string text = "Migrate " + std::to_string(convertible) +
        " asset references to identities? ";
    constexpr std::size_t NamedScenes = 3;
    std::size_t named = 0;
    for (const SceneMigrationEntry& entry : scenes)
    {
        if (entry.survey.convertible == 0)
        {
            continue;
        }
        if (named == NamedScenes)
        {
            text += "and " + std::to_string(scenes.size() - named) + " more scenes";
            break;
        }
        if (named > 0)
        {
            text += ", ";
        }
        text += entry.scenePath.filename().generic_string() + " " +
            std::to_string(entry.survey.convertible);
        ++named;
    }
    if (unresolved > 0)
    {
        // 안 바뀌는 것도 질문에 적는다. 「전부 바뀐다」고 읽고 승인하면, 남은 경로 참조는
        // 나중에 파일을 옮길 때 조용히 끊긴다.
        text += ". " + std::to_string(unresolved) + " point at nothing and stay paths";
    }
    return text + ".";
}

std::vector<GameEngine::Core::Guid> CollectReferencedGuids(
    const Runtime::Scene& scene,
    const std::span<const Serialization::ComponentSchema> schemas)
{
    std::vector<GameEngine::Core::Guid> guids;
    const auto remember = [&guids](const Assets::AssetReference& reference)
    {
        if (!reference.IsGuidReference() ||
            std::ranges::find(guids, reference.GetGuid()) != guids.end())
        {
            return;
        }
        guids.push_back(reference.GetGuid());
    };
    ForEachComponent(
        scene,
        [&remember, schemas](Runtime::Component& component)
        {
            const Serialization::PreservedComponent* const preserved = AsPreserved(component);
            if (!preserved)
            {
                return;
            }
            if (const Serialization::ComponentSchema* const schema =
                    FindSchema(schemas, preserved->GetPreservedTypeName()))
            {
                ForEachPreservedReference(
                    *preserved, *schema,
                    [&remember](const std::string&, const Assets::AssetReference& reference)
                    {
                        remember(reference);
                    });
            }
        });
    ForEachAssetReference(
        scene,
        [&remember](
            const Runtime::Component&, const Runtime::PropertyDescriptor&,
            const Assets::AssetReference& reference)
        {
            remember(reference);
        });
    return guids;
}

}
