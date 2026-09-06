#include "pch.h"
#include "ComponentSchema.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>

#include "../Assets/AssetDatabase.h"
#include "../Diagnostics/Debug.h"
#include "../Runtime/Component.h"
#include "../Runtime/ComponentType.h"

namespace GameEngine::Serialization
{

namespace
{
    /// <summary>
    /// 스키마 파일의 판이다. 형식이 바뀌면 오르고, 읽는 쪽은 자기가 모르는 판을 거절한다 —
    /// 낡은 스키마를 새 규칙으로 읽어 조용히 다른 뜻이 되는 것보다, 읽지 못했다고 말하는 편이
    /// 사람에게 쓸모 있다.
    /// </summary>
    constexpr int SchemaVersion = 1;

    /// <summary>
    /// 속성 종류의 파일 표기다. 인덱스가 PropertyKind의 열거 순서와 같아야 한다 — 표기를 값에
    /// 묶는 표가 여기 하나뿐이라, 종류를 더하면 이 배열도 함께 자란다.
    /// </summary>
    constexpr std::array<std::string_view, 9> KindNames{
        "bool", "int", "float", "string", "vector2", "vector3", "color", "asset", "enum"
    };

    [[nodiscard]] std::string_view NameOfKind(const Runtime::PropertyKind kind)
    {
        const auto index = static_cast<std::size_t>(kind);
        return index < KindNames.size() ? KindNames[index] : std::string_view{};
    }

    [[nodiscard]] bool TryParseKind(const std::string& name, Runtime::PropertyKind& kind)
    {
        for (std::size_t index = 0; index < KindNames.size(); ++index)
        {
            if (KindNames[index] == name)
            {
                kind = static_cast<Runtime::PropertyKind>(index);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] Core::Json MakeNumberArray(const std::initializer_list<float> values)
    {
        Core::Json::Array array;
        array.reserve(values.size());
        for (const float value : values)
        {
            array.emplace_back(static_cast<double>(value));
        }
        return Core::Json(std::move(array));
    }

    /// <summary>배열 JSON에서 성분들을 읽는다. 크기나 형태가 다르면 false다.</summary>
    [[nodiscard]] bool ReadNumbers(
        const Core::Json& json, const std::size_t count, std::span<float> out)
    {
        if (!json.IsArray() || json.Size() != count)
        {
            return false;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            if (!json.At(index).IsNumber())
            {
                return false;
            }
            out[index] = json.At(index).Get<float>();
        }
        return true;
    }
}

Core::Json PropertyValueToJson(
    const Runtime::PropertyKind kind, const Runtime::PropertyValue& value,
    const std::span<const std::string> enumNames)
{
    using Runtime::PropertyKind;
    switch (kind)
    {
    case PropertyKind::Bool:
        return Core::Json(std::get<bool>(value));
    case PropertyKind::Int:
        return Core::Json(static_cast<double>(std::get<int>(value)));
    case PropertyKind::Float:
        return Core::Json(static_cast<double>(std::get<float>(value)));
    case PropertyKind::String:
        return Core::Json(std::get<std::string>(value));
    case PropertyKind::Vector2:
    {
        const Math::Vector2& vector = std::get<Math::Vector2>(value);
        return MakeNumberArray({ vector.GetX(), vector.GetY() });
    }
    case PropertyKind::Vector3:
    {
        const Math::Vector3& vector = std::get<Math::Vector3>(value);
        return MakeNumberArray({ vector.GetX(), vector.GetY(), vector.GetZ() });
    }
    case PropertyKind::Color:
    {
        const Math::Color& color = std::get<Math::Color>(value);
        return MakeNumberArray({ color.r, color.g, color.b, color.a });
    }
    case PropertyKind::AssetReference:
        return Core::Json(std::get<Assets::AssetReference>(value).ToString());
    case PropertyKind::Enum:
    {
        // 파일에 적히는 것은 인덱스가 아니라 이름이다. 이름 표 밖의 값은 적을 이름이 없으므로
        // 첫 이름으로 떨어진다 — 그런 값은 서술 자체의 버그이고, 파일이 그것을 굳히지 않는다.
        const int index = std::get<int>(value);
        if (index >= 0 && static_cast<std::size_t>(index) < enumNames.size())
        {
            return Core::Json(enumNames[static_cast<std::size_t>(index)]);
        }
        return Core::Json(enumNames.empty() ? std::string{} : enumNames.front());
    }
    }
    return Core::Json{};
}

Runtime::PropertyValue PropertyValueFromJson(
    const Runtime::PropertyKind kind, const Core::Json& json,
    const std::span<const std::string> enumNames, const Runtime::PropertyValue& fallback)
{
    using Runtime::PropertyKind;
    using Runtime::PropertyValue;
    switch (kind)
    {
    case PropertyKind::Bool:
        return json.IsBoolean() ? PropertyValue{ json.Get<bool>() } : fallback;
    case PropertyKind::Int:
        return json.IsNumber() ? PropertyValue{ json.Get<int>() } : fallback;
    case PropertyKind::Float:
        return json.IsNumber() ? PropertyValue{ json.Get<float>() } : fallback;
    case PropertyKind::String:
        return json.IsString() ? PropertyValue{ json.Get<std::string>() } : fallback;
    case PropertyKind::Vector2:
    {
        std::array<float, 2> elements{};
        return ReadNumbers(json, 2, elements)
            ? PropertyValue{ Math::Vector2{ elements[0], elements[1] } }
            : fallback;
    }
    case PropertyKind::Vector3:
    {
        std::array<float, 3> elements{};
        return ReadNumbers(json, 3, elements)
            ? PropertyValue{ Math::Vector3{ elements[0], elements[1], elements[2] } }
            : fallback;
    }
    case PropertyKind::Color:
    {
        std::array<float, 4> elements{};
        if (ReadNumbers(json, 4, elements))
        {
            return PropertyValue{ Math::Color{ elements[0], elements[1], elements[2], elements[3] } };
        }
        std::array<float, 3> rgb{};
        return ReadNumbers(json, 3, rgb)
            ? PropertyValue{ Math::Color{ rgb[0], rgb[1], rgb[2], 1.0f } }
            : fallback;
    }
    case PropertyKind::AssetReference:
        return json.IsString()
            ? PropertyValue{ Assets::AssetReference::Parse(json.Get<std::string>()) }
            : fallback;
    case PropertyKind::Enum:
    {
        if (!json.IsString())
        {
            return fallback;
        }
        const std::string text = json.Get<std::string>();
        for (std::size_t index = 0; index < enumNames.size(); ++index)
        {
            if (enumNames[index] == text)
            {
                return PropertyValue{ static_cast<int>(index) };
            }
        }
        return fallback;
    }
    }
    return fallback;
}

std::string WriteComponentSchemas(const std::span<const Runtime::ComponentType* const> types)
{
    Core::Json::Array components;
    components.reserve(types.size());

    for (const Runtime::ComponentType* const type : types)
    {
        if (!type)
        {
            continue;
        }

        // 기본값은 그 타입의 기본 인스턴스가 답한다. 만들 수 없는 타입 — 생성 훅이 없는 것 —
        // 은 기본값 없이 속성만 적힌다: 에디터도 그런 타입은 추가하지 못한다.
        const std::unique_ptr<Runtime::Component> instance = type->CreateInstance();

        Core::Json::Array properties;
        for (const Runtime::PropertyDescriptor* const descriptor :
             Runtime::CollectProperties(*type))
        {
            Core::Json::Object property;
            property.emplace("name", Core::Json(std::string(descriptor->GetName())));
            property.emplace("displayName", Core::Json(std::string(descriptor->GetDisplayName())));
            property.emplace("kind", Core::Json(std::string(NameOfKind(descriptor->GetKind()))));

            const std::span<const std::string> enumNames = descriptor->GetEnumNames();
            if (!enumNames.empty())
            {
                Core::Json::Array names;
                names.reserve(enumNames.size());
                for (const std::string& name : enumNames)
                {
                    names.emplace_back(name);
                }
                property.emplace("enumNames", Core::Json(std::move(names)));
            }
            if (const std::optional<Assets::AssetType> assetType = descriptor->GetAssetType())
            {
                property.emplace(
                    "assetType",
                    Core::Json(std::string(Assets::AssetDatabase::GetAssetTypeName(*assetType))));
            }
            if (!descriptor->GetConstants().empty())
            {
                Core::Json::Object constants;
                for (const auto& [name, value] : descriptor->GetConstants())
                {
                    constants.emplace(name, Core::Json(value));
                }
                property.emplace("constants", Core::Json(std::move(constants)));
            }
            if (Runtime::HasTrait(descriptor->GetTraits(), Runtime::PropertyTraits::NotSerialized))
            {
                property.emplace("notSerialized", Core::Json(true));
            }
            if (Runtime::HasTrait(descriptor->GetTraits(), Runtime::PropertyTraits::OmitWhenInvalid))
            {
                property.emplace("omitWhenInvalid", Core::Json(true));
            }
            if (instance)
            {
                property.emplace(
                    "default",
                    PropertyValueToJson(
                        descriptor->GetKind(), descriptor->Get(*instance), enumNames));
            }
            properties.emplace_back(std::move(property));
        }

        Core::Json::Object component;
        component.emplace("type", Core::Json(std::string(type->GetName())));
        component.emplace("creatable", Core::Json(type->IsCreatable()));
        component.emplace("properties", Core::Json(std::move(properties)));
        components.emplace_back(std::move(component));
    }

    Core::Json::Object root;
    root.emplace("schemaVersion", Core::Json(static_cast<double>(SchemaVersion)));
    root.emplace("components", Core::Json(std::move(components)));
    return Core::Json(std::move(root)).Dump();
}

std::vector<ComponentSchema> ParseComponentSchemas(const std::string_view text)
{
    Core::Json root;
    try
    {
        root = Core::Json::Parse(text);
    }
    catch (const Core::JsonError& error)
    {
        Diagnostics::Debug::LogError(
            "The component schema could not be parsed; game components will not be editable. error=",
            error.what());
        return {};
    }

    const int version = root.Value("schemaVersion", 0);
    if (version != SchemaVersion)
    {
        Diagnostics::Debug::LogError(
            "The component schema was written by a different version of the engine; rebuild the "
            "game to regenerate it. found=", version, ", expected=", SchemaVersion);
        return {};
    }

    const Core::Json* const components = root.Find("components");
    if (!components || !components->IsArray())
    {
        Diagnostics::Debug::LogError("The component schema has no component list.");
        return {};
    }

    std::vector<ComponentSchema> schemas;
    schemas.reserve(components->Size());
    for (const Core::Json& componentJson : components->AsArray())
    {
        if (!componentJson.IsObject())
        {
            continue;
        }
        ComponentSchema schema;
        schema.typeName = componentJson.Value("type", std::string{});
        if (schema.typeName.empty() || !componentJson.Value("creatable", true))
        {
            // 이름이 없으면 무엇인지 알 수 없고, 만들 수 없는 타입은 에디터가 붙일 수 없다.
            continue;
        }
        if (const Core::Json* const properties = componentJson.Find("properties");
            properties && properties->IsArray())
        {
            for (const Core::Json& propertyJson : properties->AsArray())
            {
                if (!propertyJson.IsObject())
                {
                    continue;
                }
                ComponentSchemaProperty property;
                property.name = propertyJson.Value("name", std::string{});
                const std::string kindName = propertyJson.Value("kind", std::string{});
                if (property.name.empty() || !TryParseKind(kindName, property.kind))
                {
                    // 이 판이 모르는 종류다. 그 속성만 빠지고 나머지는 편집할 수 있다.
                    Diagnostics::Debug::LogWarning(
                        "A component schema property was skipped. type=", schema.typeName,
                        ", property=", property.name, ", kind=", kindName);
                    continue;
                }
                property.displayName = propertyJson.Value("displayName", property.name);
                if (const Core::Json* const names = propertyJson.Find("enumNames");
                    names && names->IsArray())
                {
                    for (const Core::Json& name : names->AsArray())
                    {
                        if (name.IsString())
                        {
                            property.enumNames.push_back(name.Get<std::string>());
                        }
                    }
                }
                if (const Core::Json* const constants = propertyJson.Find("constants");
                    constants && constants->IsObject())
                {
                    for (const auto& [name, value] : constants->AsObject())
                    {
                        if (value.IsString())
                        {
                            property.constants.emplace_back(name, value.Get<std::string>());
                        }
                    }
                }
                if (const Core::Json* const assetType = propertyJson.Find("assetType");
                    assetType && assetType->IsString())
                {
                    property.assetType =
                        Assets::AssetDatabase::ParseAssetTypeName(assetType->Get<std::string>());
                }
                property.notSerialized = propertyJson.Value("notSerialized", false);
                property.omitWhenInvalid = propertyJson.Value("omitWhenInvalid", false);
                if (const Core::Json* const defaultValue = propertyJson.Find("default"))
                {
                    property.defaultValue = *defaultValue;
                }
                schema.properties.push_back(std::move(property));
            }
        }
        schemas.push_back(std::move(schema));
    }
    return schemas;
}

Core::Json MakeDefaultComponentJson(const ComponentSchema& schema)
{
    Core::Json::Object component;
    component.emplace("type", Core::Json(schema.typeName));
    for (const ComponentSchemaProperty& property : schema.properties)
    {
        if (property.notSerialized || property.defaultValue.IsNull())
        {
            continue;
        }
        component.emplace(property.name, property.defaultValue);
        for (const auto& [name, value] : property.constants)
        {
            component.emplace(name, Core::Json(value));
        }
    }
    return Core::Json(std::move(component));
}

}
