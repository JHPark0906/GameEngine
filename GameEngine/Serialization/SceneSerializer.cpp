#include "pch.h"
#include "SceneSerializer.h"

#include "ComponentFactory.h"
#include "../Core/Json.h"
#include "PreservedComponent.h"

#include "../Runtime/Canvas.h"
#include "../Runtime/ComponentType.h"
#include "../Runtime/PropertyDescriptor.h"
#include "../Runtime/RectTransform.h"
#include "../Runtime/Scene.h"
#include "../Runtime/Transform.h"
#include "../Diagnostics/Debug.h"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace GameEngine::Serialization
{

namespace
{
    void AddComponents(const Core::Json& gameObjectJson, Runtime::GameObject& gameObject)
    {
        const Core::Json* components = gameObjectJson.Find("components");
        if (!components || !components->IsArray())
        {
            return;
        }
        for (const Core::Json& componentJson : components->AsArray())
        {
            // 만들지 못한 컴포넌트는 아래에서 로그로 말한다. 장면 로드는 그것 때문에 멈추지 않는다.
            static_cast<void>(
                SceneSerializer::LoadComponentIntoGameObject(componentJson, gameObject));
        }
    }

    struct LoadedGameObject
    {
        Runtime::GameObject* gameObject = nullptr;
        std::optional<unsigned int> id;
        std::optional<unsigned int> parentId;
    };

    /// <summary>
    /// 저장에 쓸 번호를 미리 정한다. 파일에서 읽은 오브젝트는 그 번호를 유지한다.
    ///
    /// 새 오브젝트에 GetInstanceId()를 그대로 쓰면 다른 세션에서 저장한 번호와 충돌할 수 있다.
    /// 중복 id가 저장되면 로더가 장면을 거부하므로, 새 번호는 장면의 모든 기존 번호보다 큰
    /// 값에서 시작해 겹치지 않게 순서대로 매긴다.
    /// </summary>
    [[nodiscard]] std::unordered_map<const Runtime::GameObject*, unsigned int> AssignSavedIds(
        const std::vector<const Runtime::GameObject*>& gameObjects)
    {
        std::unordered_map<const Runtime::GameObject*, unsigned int> idToWrite;
        idToWrite.reserve(gameObjects.size());

        unsigned int highestFixedId = 0;
        std::vector<const Runtime::GameObject*> unassigned;
        for (const Runtime::GameObject* const gameObject : gameObjects)
        {
            if (const std::optional<unsigned int> fixedId = gameObject->GetSerializedId())
            {
                idToWrite.emplace(gameObject, *fixedId);
                highestFixedId = (std::max)(highestFixedId, *fixedId);
            }
            else
            {
                unassigned.push_back(gameObject);
            }
        }

        // 순서를 정해 둔다: 그러지 않으면 같은 미배정 오브젝트 집합이라도 컨테이너 순회 순서에
        // 따라 매번 다른 번호를 받고, 그것도 「안 바꿨는데 파일이 달라지는」 같은 부류의 잡음이다.
        std::ranges::sort(unassigned, {}, &Runtime::GameObject::GetInstanceId);
        unsigned int nextId = highestFixedId;
        for (const Runtime::GameObject* const gameObject : unassigned)
        {
            idToWrite.emplace(gameObject, ++nextId);
        }
        return idToWrite;
    }
}

namespace
{
    /// <summary>
    /// 화면 UI 컴포넌트에 배치의 뿌리인 조상 Canvas가 없으면 로그로 알린다.
    ///
    /// Canvas를 자동으로 추가하면 장면 구조와 잘림 범위를 바꾸므로 여기서는 진단만 한다.
    /// 계층을 고치는 일은 사용자의 몫이다.
    /// </summary>
    void WarnAboutScreenUiWithoutCanvas(
        const Runtime::Scene& scene, const std::filesystem::path& sceneFile)
    {
        for (const auto& entry : scene.GetGameObjects() | std::views::values)
        {
            const Runtime::GameObject* const gameObject = entry.get();
            if (gameObject == nullptr ||
                gameObject->GetComponent<Runtime::RectTransform>() == nullptr)
            {
                continue;
            }
            bool underCanvas = false;
            for (const Runtime::Transform* node = &gameObject->GetTransform(); node != nullptr;
                node = node->GetParent())
            {
                const Runtime::GameObject* const owner = node->GetGameObject();
                if (owner != nullptr && owner->GetComponent<Runtime::Canvas>() != nullptr)
                {
                    underCanvas = true;
                    break;
                }
            }
            if (!underCanvas)
            {
                Diagnostics::Debug::LogWarning(
                    "This object has a screen rectangle but no Canvas above it, so nothing will"
                    " resolve its rectangle and it will neither draw nor take the pointer."
                    " file=", sceneFile.string(), ", object=", gameObject->GetName());
            }
        }
    }
}

std::unique_ptr<Runtime::Scene> SceneSerializer::LoadFromBytes(
    const std::span<const std::byte> sceneBytes,
    const std::filesystem::path& sceneFile,
    Runtime::RuntimeContext& runtimeContext)
{
    Core::Json jsonRoot;
    try
    {
        jsonRoot = Core::Json::ParseBytes(sceneBytes);
    }
    catch (const std::exception& exception)
    {
        Diagnostics::Debug::LogError(exception.what());
        Diagnostics::Debug::LogError("Failed to parse scene file: ", sceneFile.string());
        return nullptr;
    }

    try
    {
        // 장면의 이름은 파일 이름이다. 파일 안의 sceneName은 그것을 비추는 그림자이고, 읽을
        // 때마다 파일 이름으로 다시 맞춘다.
        //
        // 이렇게 하는 이유는 두 이름이 갈라질 수 있기 때문이다. 계층은 목록 행에 <b>경로</b>를,
        // 열린 장면 머리글에 <b>sceneName</b>을 쓰므로, 탐색기에서 파일 이름을 바꾸면 같은
        // 장면이 화면에서 두 이름으로 보인다. 둘 중 하나를 진실로 정해야 하고, 파일 이름이
        // 진실이어야 한다 — 사람이 밖에서 바꿀 수 있는 쪽이 그것이고, 그때 따라오지 못하는
        // 이름은 틀린 이름이 된다.
        //
        // sceneName은 어떤 참조의 열쇠도 아니어서(표시와 로그에만 쓰인다) 이렇게 덮어도 잃는
        // 것이 없고, 형식이 바뀌지 않으므로 옛 프로젝트도 그대로 열린다.
        const std::string nameFromFile = sceneFile.stem().string();
        auto scene = std::make_unique<Runtime::Scene>(
            runtimeContext,
            nameFromFile.empty() ? jsonRoot.Value("sceneName", std::string{}) : nameFromFile);

        const Core::Json* gameObjects = jsonRoot.Find("gameObjects");
        if (!gameObjects || !gameObjects->IsArray())
        {
            Diagnostics::Debug::LogWarning("Scene file has no valid gameObjects array: ", sceneFile.string());
            return scene;
        }

        std::vector<LoadedGameObject> loadedObjects;
        std::unordered_map<unsigned int, Runtime::GameObject*> objectsBySerializedId;
        for (const Core::Json& gameObjectJson : gameObjects->AsArray())
        {
            if (!gameObjectJson.IsObject())
            {
                continue;
            }

            const std::string gameObjectName = gameObjectJson.Value("name", std::string{});
            auto gameObject = std::make_unique<Runtime::GameObject>(runtimeContext, gameObjectName);
            gameObject->SetActive(gameObjectJson.Value("isActive", true));
            AddComponents(gameObjectJson, *gameObject);

            LoadedGameObject loaded;
            if (const Core::Json* id = gameObjectJson.Find("id"))
            {
                loaded.id = id->Get<unsigned int>();
            }
            if (const Core::Json* parent = gameObjectJson.Find("parent"))
            {
                loaded.parentId = parent->Get<unsigned int>();
            }

            loaded.gameObject = scene->AddGameObject(std::move(gameObject));
            if (!loaded.gameObject)
                throw Core::JsonError("failed to add GameObject to scene");
            // 파일이 이 오브젝트를 가리키던 번호를 들고 있는다. 이 프로세스의 레지스트리가 매긴
            // GetInstanceId()는 세션마다 달라질 수 있지만, 저장은 이 번호를 그대로 돌려준다 —
            // 그러지 않으면 아무것도 안 바꾸고 열었다 저장하기만 해도 모든 오브젝트의 id와
            // parent가 다시 매겨진다.
            if (loaded.id)
                loaded.gameObject->SetSerializedId(*loaded.id);
            if (loaded.id && !objectsBySerializedId.emplace(*loaded.id, loaded.gameObject).second)
                throw Core::JsonError("duplicate GameObject id: " + std::to_string(*loaded.id));
            loadedObjects.push_back(loaded);
        }

        for (const LoadedGameObject& loaded : loadedObjects)
        {
            if (!loaded.parentId)
                continue;
            const auto parent = objectsBySerializedId.find(*loaded.parentId);
            if (parent == objectsBySerializedId.end())
                throw Core::JsonError("unknown parent GameObject id: " + std::to_string(*loaded.parentId));
            if (!loaded.gameObject->GetTransform().SetParent(&parent->second->GetTransform()))
                throw Core::JsonError("invalid Transform hierarchy for GameObject: " + loaded.gameObject->GetName());
        }

        WarnAboutScreenUiWithoutCanvas(*scene, sceneFile);
        Diagnostics::Debug::Log("Scene data loaded from file: ", sceneFile.string());
        return scene;
    }
    catch (const std::exception& exception)
    {
        Diagnostics::Debug::LogError(
            "Failed to construct scene data from ", sceneFile.string(), ": ", exception.what());
        return nullptr;
    }
}

namespace
{
    void WriteEscapedJsonString(std::ostream& stream, const std::string_view value)
    {
        stream << '"';
        constexpr char HexDigits[] = "0123456789abcdef";
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '"': stream << "\\\""; break;
            case '\\': stream << "\\\\"; break;
            case '\b': stream << "\\b"; break;
            case '\f': stream << "\\f"; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default:
                if (character < 0x20)
                {
                    stream << "\\u00" << HexDigits[character >> 4] << HexDigits[character & 0x0f];
                }
                else
                {
                    stream << static_cast<char>(character);
                }
                break;
            }
        }
        stream << '"';
    }

    void WriteVector3(std::ostream& stream, const Math::Vector3& value)
    {
        stream << '[' << value.GetX() << ", " << value.GetY() << ", " << value.GetZ() << ']';
    }

    void WriteVector2(std::ostream& stream, const Math::Vector2& value)
    {
        stream << '[' << value.GetX() << ", " << value.GetY() << ']';
    }

    void WriteColor(std::ostream& stream, const Math::Color& value)
    {
        stream << '[' << value.r << ", " << value.g << ", " << value.b << ", " << value.a << ']';
    }

    /// <summary>속성 값 하나를 종류의 JSON 표현으로 쓴다. 이름 표 밖의 enum 값이면 false다.</summary>
    [[nodiscard]] bool WritePropertyValue(
        std::ostream& stream,
        const Runtime::PropertyDescriptor& descriptor,
        const Runtime::PropertyValue& value)
    {
        using Runtime::PropertyKind;
        switch (descriptor.GetKind())
        {
        case PropertyKind::Bool:
            stream << (std::get<bool>(value) ? "true" : "false");
            return true;
        case PropertyKind::Int:
            stream << std::get<int>(value);
            return true;
        case PropertyKind::Float:
            stream << std::get<float>(value);
            return true;
        case PropertyKind::String:
            WriteEscapedJsonString(stream, std::get<std::string>(value));
            return true;
        case PropertyKind::Vector2:
            WriteVector2(stream, std::get<Math::Vector2>(value));
            return true;
        case PropertyKind::Vector3:
            WriteVector3(stream, std::get<Math::Vector3>(value));
            return true;
        case PropertyKind::Color:
            WriteColor(stream, std::get<Math::Color>(value));
            return true;
        case PropertyKind::AssetReference:
            WriteEscapedJsonString(stream, std::get<Assets::AssetReference>(value).ToString());
            return true;
        case PropertyKind::Enum:
        {
            const int index = std::get<int>(value);
            const std::span<const std::string> names = descriptor.GetEnumNames();
            if (index < 0 || static_cast<std::size_t>(index) >= names.size())
            {
                return false;
            }
            WriteEscapedJsonString(stream, names[static_cast<std::size_t>(index)]);
            return true;
        }
        }
        return false;
    }

    /// <summary>
    /// 컴포넌트 하나를 타입 사슬의 속성 서술로 쓴다. 서술이 곧 파일 형식이다.
    /// 로더가 이름으로 만들 수 없고 Transform도 아닌 타입이면 false를 반환하고 호출자가 경고한다.
    /// </summary>
    [[nodiscard]] bool WriteComponent(std::ostream& stream, const Runtime::Component& component)
    {
        // 보존된 컴포넌트는 실려 온 JSON 그대로 돌아간다. 그 장면을 팩토리가 있는 프로세스가
        // 다시 열면 원래 컴포넌트가 되살아난다.
        if (const auto* preserved = dynamic_cast<const PreservedComponent*>(&component))
        {
            stream << preserved->GetData().Dump();
            return true;
        }

        const Runtime::ComponentType& type = component.GetComponentType();
        if (!type.IsCreatable() && &type != &Runtime::Transform::StaticType())
        {
            return false;
        }

        stream << "{\"type\": ";
        WriteEscapedJsonString(stream, type.GetName());
        for (const Runtime::PropertyDescriptor* const descriptor :
             Runtime::CollectProperties(type))
        {
            if (Runtime::HasTrait(descriptor->GetTraits(), Runtime::PropertyTraits::NotSerialized))
            {
                continue;
            }
            const Runtime::PropertyValue value = descriptor->Get(component);
            if (Runtime::HasTrait(descriptor->GetTraits(), Runtime::PropertyTraits::OmitWhenInvalid))
            {
                if (const auto* const reference = std::get_if<Assets::AssetReference>(&value);
                    reference && !reference->IsValid())
                {
                    continue;
                }
            }
            stream << ", \"" << descriptor->GetName() << "\": ";
            if (!WritePropertyValue(stream, *descriptor, value))
            {
                Diagnostics::Debug::LogError(
                    "A property value cannot be written in its declared kind. type=",
                    type.GetName(), ", property=", descriptor->GetName());
                return false;
            }
            for (const auto& [constantName, constantValue] : descriptor->GetConstants())
            {
                stream << ", \"" << constantName << "\": ";
                WriteEscapedJsonString(stream, constantValue);
            }
        }

        // 속성으로 기술할 수 없는 상태 — 타일맵의 레이어 배열 — 는 컴포넌트가 직접 쓴다.
        // 멤버 이름을 정렬해 내보내므로, 같은 장면은 같은 텍스트가 된다.
        Core::Json::Object extra;
        component.WriteExtraSerializedState(extra);
        std::vector<const std::string*> extraNames;
        extraNames.reserve(extra.size());
        for (const auto& member : extra)
        {
            extraNames.push_back(&member.first);
        }
        std::ranges::sort(extraNames, {}, [](const std::string* name) { return *name; });
        for (const std::string* const name : extraNames)
        {
            stream << ", ";
            WriteEscapedJsonString(stream, *name);
            stream << ": " << extra.at(*name).Dump();
        }
        stream << '}';

        return true;
    }
}

Runtime::SceneLoader SceneSerializer::MakeSceneLoader()
{
    return [](const std::vector<std::byte>& bytes, const std::filesystem::path& relativePath,
              Runtime::RuntimeContext& runtimeContext)
    {
        return LoadFromBytes(bytes, relativePath, runtimeContext);
    };
}

std::string SceneSerializer::SaveToText(const Runtime::Scene& scene)
{
    std::ostringstream stream;
    stream << std::setprecision(std::numeric_limits<float>::max_digits10);

    stream << "{\n  \"sceneName\": ";
    WriteEscapedJsonString(stream, scene.GetName());
    stream << ",\n  \"gameObjects\": [";

    // 저장이 쓸 번호 순으로 쓴다: 장면의 저장 컨테이너는 순서를 약속하지 않으므로, 정렬 없이는
    // 같은 장면이 저장할 때마다 다른 파일이 됐을 것이다. GetInstanceId()가 아니라 이 번호로
    // 정렬하는 것이 중요하다 — 파일에서 읽은 오브젝트는 그 번호가 안정적이지만 GetInstanceId()는
    // 세션마다 다를 수 있어서, 그것으로 정렬하면 id는 그대로인데 줄 순서만 바뀌는 diff가 남는다.
    std::vector<const Runtime::GameObject*> gameObjects;
    for (const auto& gameObject : scene.GetGameObjects() | std::views::values)
    {
        gameObjects.push_back(gameObject.get());
    }
    const std::unordered_map<const Runtime::GameObject*, unsigned int> idToWrite =
        AssignSavedIds(gameObjects);
    std::ranges::sort(gameObjects, {}, [&idToWrite](const Runtime::GameObject* const gameObject)
    {
        return idToWrite.at(gameObject);
    });

    bool firstObject = true;
    for (const Runtime::GameObject* const gameObject : gameObjects)
    {
        stream << (firstObject ? "\n" : ",\n") << "    {\"id\": " << idToWrite.at(gameObject)
               << ", \"name\": ";
        WriteEscapedJsonString(stream, gameObject->GetName());
        stream << ", \"isActive\": " << (gameObject->IsActive() ? "true" : "false");
        firstObject = false;

        // 부모는 같은 장면 안의 객체일 때만 기록한다. 로더가 id를 쓰는 곳이 정확히 이 연결이다.
        // 부모를 가리키는 이 번호는 부모 자신의 항목에 적힌 id와 같은 것이어야 하므로 반드시
        // 같은 idToWrite에서 얻는다.
        if (const Runtime::Transform* const parent = gameObject->GetTransform().GetParent())
        {
            if (const Runtime::GameObject* const parentObject = parent->GetGameObject();
                parentObject && parentObject->GetScene() == &scene)
            {
                stream << ", \"parent\": " << idToWrite.at(parentObject);
            }
        }

        stream << ", \"components\": [";
        bool firstComponent = true;
        for (const std::unique_ptr<Runtime::Component>& component : gameObject->GetAllComponents())
        {
            std::ostringstream componentStream;
            componentStream << std::setprecision(std::numeric_limits<float>::max_digits10);
            if (!WriteComponent(componentStream, *component))
            {
                Diagnostics::Debug::LogWarning(
                    "A component the engine cannot serialize was skipped. object=",
                    gameObject->GetName(),
                    ", type=", component->GetComponentType().GetName());
                continue;
            }
            stream << (firstComponent ? "" : ", ") << componentStream.str();
            firstComponent = false;
        }
        stream << "]}";
    }

    stream << (gameObjects.empty() ? "]" : "\n  ]") << "\n}\n";
    return stream.str();
}

std::optional<Core::Json> SceneSerializer::SaveComponentToJson(
    const Runtime::Component& component)
{
    std::ostringstream stream;
    // 장면 저장과 같은 정밀도다: 부동소수는 왕복이 보장되는 자릿수로 써야, 저장했다 읽은 값이
    // 원래 값과 같다고 말할 수 있다.
    stream << std::setprecision(std::numeric_limits<float>::max_digits10);
    if (!WriteComponent(stream, component))
    {
        return std::nullopt;
    }
    return Core::Json::Parse(stream.str());
}

Runtime::Component* SceneSerializer::LoadComponentIntoGameObject(
    const Core::Json& componentJson, Runtime::GameObject& gameObject)
{
    if (!componentJson.IsObject())
    {
        Diagnostics::Debug::LogWarning("Skipping a component entry that is not an object.");
        return nullptr;
    }
    const std::string type = componentJson.Value("type", std::string{});

    // 팩토리가 없는 타입은 잃는 대신 보존한다. 프로젝트가 정의한 컴포넌트를 에디터가 열 때가
    // 바로 이 경우이고, 떨어뜨리면 그 장면을 저장하는 순간 스크립트가 사라진다. 팩토리는 있는데
    // 만들다 실패한 것은 보존하지 않는다: 그 JSON은 깨진 데이터이고, 보존은 그것을 굳히는 셈이다.
    // 값 하나가 형식을 벗어난 것은 또 다른 이야기이고, 아래의 ComponentValueError가 그것을 다룬다.
    if (!type.empty() && !ComponentFactory::IsRegistered(type))
    {
        auto preserved = std::make_unique<PreservedComponent>();
        preserved->SetData(componentJson);
        Runtime::Component* const added = gameObject.AddComponent(std::move(preserved));
        if (added)
        {
            Diagnostics::Debug::LogWarning(
                "A component type this process cannot create was preserved as data: ", type);
        }
        return added;
    }

    const std::size_t before = gameObject.GetAllComponents().size();
    try
    {
        if (!ComponentFactory::Create(type, componentJson, gameObject))
        {
            Diagnostics::Debug::LogWarning(
                "Unknown component type or component creation failed: ", type);
            return nullptr;
        }
    }
    // 값 하나의 형식 오류는 해당 컴포넌트만 보존 데이터로 강등한다.
    // 장면 전체의 뜻에 영향을 주는 오류, 예를 들어 다른 회전 표기를 말하는 상수 주석은
    // ComponentValueError가 아니므로 호출자에게 전파되어 장면 로드를 중단한다.
    catch (const ComponentValueError& error)
    {
        Diagnostics::Debug::LogWarning(
            "A component could not be read and was preserved as data. type=", type,
            ", error=", error.what());

        // 팩토리가 이미 부착까지 마친 뒤에 던졌다면 보존본은 같은 컴포넌트의 사본이 된다.
        // Transform도 마찬가지다: 객체가 이미 하나를 가지고 있어서 팩토리는 만들지 않고 그것을
        // 채운다.
        const std::vector<std::unique_ptr<Runtime::Component>>& attached =
            gameObject.GetAllComponents();
        if (attached.size() != before)
        {
            return attached.back().get();
        }
        if (type == Runtime::Transform::StaticType().GetName())
        {
            return &gameObject.GetTransform();
        }
        auto preserved = std::make_unique<PreservedComponent>();
        preserved->SetData(componentJson);
        return gameObject.AddComponent(std::move(preserved));
    }
    // 붙은 것이 없는데 성공했다면 Transform이다: 객체의 일부라 새로 만들지 않고 이미 있는 것이
    // 채워진다. 호출자는 그래도 "이 JSON이 된 컴포넌트"를 받아야 별칭을 이을 수 있다.
    const std::vector<std::unique_ptr<Runtime::Component>>& components =
        gameObject.GetAllComponents();
    return components.size() > before ? components.back().get() : &gameObject.GetTransform();
}
}
