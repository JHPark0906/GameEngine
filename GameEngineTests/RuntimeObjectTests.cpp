#include "RuntimeObjectTests.h"

#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "Assets/AssetReference.h"
#include "Math/Color.h"
#include "Math/Vector.h"
#include "Platform/IInput.h"
#include "Runtime/AudioSource.h"
#include "Runtime/Camera.h"
#include "Runtime/Component.h"
#include "Runtime/ComponentType.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/Light.h"
#include "Runtime/MeshRenderer.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/PropertyDescriptor.h"
#include "Runtime/Renderer.h"
#include "Runtime/Rigidbody2D.h"
#include "Runtime/Rigidbody3D.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Assets/Asset.h"
#include "Runtime/SpriteAnimator.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/TilemapRenderer.h"
#include "Serialization/ComponentFactory.h"
#include "Serialization/ComponentSchema.h"
#include "Serialization/PreservedComponent.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Serialization/SceneSerializer.h"
#include "Core/Json.h"
#include "Runtime/Transform.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>
    /// 플랫폼 입력을 대신해, 런타임의 에지 감지를 창 없이 테스트할 수 있게 한다. 읽기를
    /// 파괴적으로 만드는 한 가지를 모델링한다: 텍스트와 휠 움직임은 마지막 읽기 이후의 집계라서,
    /// 읽으면 가져가진다.
    /// </summary>
    class FakeInput final : public GameEngine::Platform::IInput
    {
    public:
        void ReadState(GameEngine::Platform::InputState& state) override
        {
            state = mState;
            mState.TakeAccumulated();
        }

        void SetKey(const GameEngine::Platform::Key key, const bool isDown)
        {
            mState.SetKey(key, isDown);
        }

        void SetMouseButton(const GameEngine::Platform::MouseButton button, const bool isDown)
        {
            mState.SetMouseButton(button, isDown);
        }

        void SetCursor(const int x, const int y) { mState.cursor = { x, y }; }
        void SetFocus(const bool hasFocus) { mState.hasFocus = hasFocus; }
        void Type(const std::string& text) { mState.typedText += text; }
        void Scroll(const float notches) { mState.wheelDelta += notches; }

    private:
        GameEngine::Platform::InputState mState;
    };
}

/// <summary>
/// 두 레지스트리가 공존하고, 객체는 자기 id를 발급한 쪽에 돌려준다.
///
/// 이 테스트에 Game이 아예 필요 없다는 것이 요점이다. 런타임 객체가 Game만이 설치하는 프로세스
/// 전역에서 레지스트리를 읽으면, Game 밖에서 만들 때 던지므로 객체 모델을 홀로 시험할 수 없다.
/// id는 레지스트리마다 1부터 다시 시작하기도 해서, 설치된 전역을 통해 반납하면 다른
/// 레지스트리에서 그 id를 쥔 무관한 살아 있는 객체를 지우게 된다.
/// </summary>
bool RunObjectRegistryLifetimeTests()
{
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::ObjectRegistry;

    ObjectRegistry firstRegistry;
    ObjectRegistry secondRegistry;
    const GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext firstContext(firstRegistry, input);
    GameEngine::Runtime::RuntimeContext secondContext(secondRegistry, input);

    auto firstObject = std::make_unique<GameObject>(firstContext, "First");
    auto secondObject = std::make_unique<GameObject>(secondContext, "Second");
    const unsigned int firstId = firstObject->GetInstanceId();
    const unsigned int secondId = secondObject->GetInstanceId();

    const bool idsCollide = firstId == secondId;
    const bool bothRegistered =
        firstRegistry.FindObject(firstId) == firstObject.get() &&
        secondRegistry.FindObject(secondId) == secondObject.get();

    firstObject.reset();
    const bool secondSurvived = secondRegistry.FindObject(secondId) == secondObject.get();
    const bool firstReleased = firstRegistry.FindObject(firstId) == nullptr;

    return Expect(
               idsCollide,
               "two registries should issue the same first id, which is what makes this a hazard") &&
        Expect(bothRegistered, "an object should be findable in the registry it was given") &&
        Expect(
            secondSurvived,
            "destroying an object should not unregister another registry's object of that id") &&
        Expect(firstReleased, "destroying an object should release its own id");
}

/// <summary>
/// 컴포넌트 쿼리는 RTTI가 아니라 각 컴포넌트 클래스가 선언한 타입으로 답한다.
///
/// 고정해 둘 가치가 있는 케이스는 기반 클래스 쿼리다: Renderer를 물어도 MeshRenderer와
/// SpriteRenderer를 여전히 찾아야 한다. 타입을 동등 비교하는 색인이라면 그 질문에 아무것도
/// 답하지 못하면서, 엔진이 마침 오늘 하는 모든 말단 타입 쿼리에서는 완벽히 옳아 보였을
/// 것이다.
/// </summary>
bool RunComponentTypeTests()
{
    using namespace GameEngine::Runtime;

    // No Game either: a runtime object needs the services a component may reach, nothing more.
    ObjectRegistry registry;
    const Input input;
    RuntimeContext runtimeContext(registry, input);
    auto object = std::make_unique<GameObject>(runtimeContext, "Typed");

    MeshRenderer* const mesh = object->AddComponent<MeshRenderer>();
    SpriteRenderer* const sprite = object->AddComponent<SpriteRenderer>();
    Camera* const camera = object->AddComponent<Camera>();
    if (!mesh || !sprite || !camera)
    {
        return Expect(false, "the test object should accept its components");
    }

    const GameObject& constObject = *object;
    // Not const: a filter view caches its first position, so iterating one mutates it.
    auto renderers = constObject.GetComponents<Renderer>();
    auto meshes = constObject.GetComponents<MeshRenderer>();
    auto everything = constObject.GetComponents<Component>();

    const bool leafQueries =
        object->GetComponent<MeshRenderer>() == mesh &&
        object->GetComponent<Camera>() == camera &&
        object->GetComponent<AudioSource>() == nullptr;

    // A GameObject always carries a Transform, so the whole-hierarchy query sees four.
    const bool hierarchyQueries =
        std::ranges::distance(renderers) == 2 &&
        std::ranges::distance(meshes) == 1 &&
        std::ranges::distance(everything) == 4;

    const bool chain =
        MeshRenderer::StaticType().IsDerivedFrom(Renderer::StaticType()) &&
        MeshRenderer::StaticType().IsDerivedFrom(Component::StaticType()) &&
        !Camera::StaticType().IsDerivedFrom(Renderer::StaticType()) &&
        !Renderer::StaticType().IsDerivedFrom(MeshRenderer::StaticType());

    const bool names =
        mesh->GetComponentType().GetName() == "MeshRenderer" &&
        camera->GetComponentType().GetName() == "Camera" &&
        object->GetTransform().GetComponentType().GetName() == "Transform";

    object.reset();

    return Expect(leafQueries, "a leaf-type query should find exactly its own component") &&
        Expect(
            hierarchyQueries,
            "a base-type query should find every component deriving from it") &&
        Expect(chain, "a type chain should report derivation in one direction only") &&
        Expect(names, "a component type should carry its class name");
}

/// <summary>
/// 플랫폼은 눌려 있는 것을 보고하고, 게임은 무슨 일이 있었는지 묻는다. 앞의 것을 뒤의 것으로
/// 바꾸는 일은 두 프레임에 대한 질문이고, 플랫폼마다가 아니라 여기서 한 번 답해진다. 그것이
/// 모든 플랫폼이 프레임이 언제 시작하는지에 대한 자기 생각을 갖지 않게 한다.
/// </summary>
bool RunInputTests()
{
    using GameEngine::Platform::Key;
    using GameEngine::Platform::MouseButton;
    using GameEngine::Runtime::Input;

    FakeInput source;
    Input input;
    source.SetFocus(true);

    // Nothing has happened yet, and in particular nothing reads as just-released.
    input.BeginFrame(source);
    const bool quietAtRest = !input.GetKey(Key::Space) && !input.GetKeyDown(Key::Space) &&
        !input.GetKeyUp(Key::Space);

    source.SetKey(Key::Space, true);
    input.BeginFrame(source);
    const bool pressed = input.GetKey(Key::Space) && input.GetKeyDown(Key::Space) &&
        !input.GetKeyUp(Key::Space);

    // Held is not pressed. A game that moves on GetKeyDown must move once per press.
    input.BeginFrame(source);
    const bool heldIsNotPressed = input.GetKey(Key::Space) && !input.GetKeyDown(Key::Space);

    source.SetKey(Key::Space, false);
    input.BeginFrame(source);
    const bool released = !input.GetKey(Key::Space) && input.GetKeyUp(Key::Space) &&
        !input.GetKeyDown(Key::Space);

    input.BeginFrame(source);
    const bool releaseIsOneFrame = !input.GetKeyUp(Key::Space);

    // A key the engine does not name must never read as held, whatever the platform put in the
    // array, because Unknown is what an unmapped key maps to.
    const bool unknownIsNeverDown = !input.GetKey(Key::Unknown) &&
        !input.GetKeyDown(Key::Unknown);

    source.SetMouseButton(MouseButton::Left, true);
    input.BeginFrame(source);
    const bool mousePressed = input.GetMouseButton(MouseButton::Left) &&
        input.GetMouseButtonDown(MouseButton::Left);
    source.SetMouseButton(MouseButton::Left, false);
    input.BeginFrame(source);
    const bool mouseReleased = !input.GetMouseButton(MouseButton::Left) &&
        input.GetMouseButtonUp(MouseButton::Left);

    // Typed text and wheel motion belong to the frame they arrived in and are gone the next.
    source.Type("hi");
    source.Scroll(2.0f);
    input.BeginFrame(source);
    const bool tookTypedText = input.GetTypedText() == "hi" && input.GetMouseWheel() == 2.0f;
    input.BeginFrame(source);
    const bool typedTextIsOneFrame = input.GetTypedText().empty() &&
        input.GetMouseWheel() == 0.0f;

    source.SetCursor(10, 20);
    input.BeginFrame(source);
    source.SetCursor(14, 17);
    input.BeginFrame(source);
    const bool reportsMotion = input.GetMousePosition().GetX() == 14 &&
        input.GetMousePosition().GetY() == 17 &&
        input.GetMouseDelta().GetX() == 4 && input.GetMouseDelta().GetY() == -3;

    // The cursor may be moved by someone working in another window. Reporting the distance it
    // covered while away as motion would fling whatever was being dragged when they came back.
    source.SetFocus(false);
    input.BeginFrame(source);
    source.SetCursor(400, 400);
    source.SetFocus(true);
    input.BeginFrame(source);
    const bool ignoresMotionAcrossFocus = input.GetMouseDelta().GetX() == 0 &&
        input.GetMouseDelta().GetY() == 0 && input.GetMousePosition().GetX() == 400;

    return Expect(quietAtRest, "nothing should read as pressed or released before anything happens") &&
        Expect(pressed, "a key going down should read as held and as pressed this frame") &&
        Expect(heldIsNotPressed, "a key held from an earlier frame should not read as pressed") &&
        Expect(released, "a key coming up should read as released this frame") &&
        Expect(releaseIsOneFrame, "a release should read as released for exactly one frame") &&
        Expect(unknownIsNeverDown, "an unnamed key should never read as held") &&
        Expect(mousePressed && mouseReleased, "a mouse button should report the same edges") &&
        Expect(tookTypedText, "typed text and wheel motion should arrive in their frame") &&
        Expect(typedTextIsOneFrame, "typed text and wheel motion should not arrive twice") &&
        Expect(reportsMotion, "the cursor should report its position and how far it moved") &&
        Expect(
            ignoresMotionAcrossFocus,
            "motion while another window had focus should not read as motion here");
}

namespace
{
    std::span<const GameEngine::Runtime::PropertyDescriptor> ChainBaseProperties();
    std::span<const GameEngine::Runtime::PropertyDescriptor> ChainDerivedProperties();

    /// <summary>
    /// 기반과 파생이 각자 속성을 하나씩 선언하는 사슬이다. 엔진 컴포넌트의 선언과 무관하게 사슬
    /// 열람 자체를 이 한 쌍으로 고정한다 — 엔진 쪽 사슬은 아래 선언 테스트가 따로 고정한다.
    /// </summary>
    class ChainBaseComponent : public GameEngine::Runtime::Component
    {
    public:
        [[nodiscard]] static const GameEngine::Runtime::ComponentType& StaticType()
        {
            static const GameEngine::Runtime::ComponentType type{
                "ChainBaseComponent", &GameEngine::Runtime::Component::StaticType(),
                &ChainBaseProperties };
            return type;
        }
        [[nodiscard]] const GameEngine::Runtime::ComponentType& GetComponentType() const override
        {
            return StaticType();
        }

        [[nodiscard]] float GetBaseValue() const { return mBaseValue; }
        void SetBaseValue(const float value) { mBaseValue = value; }

    private:
        [[nodiscard]] std::unique_ptr<GameEngine::Runtime::Component> Clone() const override
        {
            return nullptr;
        }

        float mBaseValue = 1.0f;
    };

    class ChainDerivedComponent final : public ChainBaseComponent
    {
    public:
        [[nodiscard]] static const GameEngine::Runtime::ComponentType& StaticType()
        {
            static const GameEngine::Runtime::ComponentType type{
                "ChainDerivedComponent", &ChainBaseComponent::StaticType(),
                &ChainDerivedProperties };
            return type;
        }
        [[nodiscard]] const GameEngine::Runtime::ComponentType& GetComponentType() const override
        {
            return StaticType();
        }

        [[nodiscard]] const std::string& GetLabel() const { return mLabel; }
        void SetLabel(std::string label) { mLabel = std::move(label); }

    private:
        [[nodiscard]] std::unique_ptr<GameEngine::Runtime::Component> Clone() const override
        {
            return nullptr;
        }

        std::string mLabel;
    };

    std::span<const GameEngine::Runtime::PropertyDescriptor> ChainBaseProperties()
    {
        static const GameEngine::Runtime::PropertyDescriptor properties[] = {
            GameEngine::Runtime::MakeProperty<ChainBaseComponent>(
                "baseValue", "Base Value",
                &ChainBaseComponent::GetBaseValue, &ChainBaseComponent::SetBaseValue),
        };
        return properties;
    }

    std::span<const GameEngine::Runtime::PropertyDescriptor> ChainDerivedProperties()
    {
        static const GameEngine::Runtime::PropertyDescriptor properties[] = {
            GameEngine::Runtime::MakeProperty<ChainDerivedComponent>(
                "label", "Label",
                &ChainDerivedComponent::GetLabel, &ChainDerivedComponent::SetLabel),
        };
        return properties;
    }
}

/// <summary>
/// ComponentType 곁의 속성 서술이 열람·접근·생성의 출처인지 확인한다.
/// 직렬화 쓰기·읽기, 인스펙터, Add Component 목록은 같은 서술을 사용하므로
/// 서술의 오류는 그 소비자들에 함께 영향을 준다.
/// </summary>
bool RunPropertyDescriptorTests()
{
    using namespace GameEngine::Runtime;

    // Light의 자기 선언: 이름·종류·선언 순서, 그리고 enum 이름 표.
    const std::span<const PropertyDescriptor> own = Light::StaticType().GetOwnProperties();
    const bool declaresOwn = own.size() == 4 &&
        own[0].GetName() == "kind" && own[0].GetKind() == PropertyKind::Enum &&
        own[1].GetName() == "color" && own[1].GetKind() == PropertyKind::Color &&
        own[2].GetName() == "intensity" && own[2].GetKind() == PropertyKind::Float &&
        own[3].GetName() == "range" && own[3].GetKind() == PropertyKind::Float;
    const bool enumNamesMatchSceneFormat = declaresOwn &&
        own[0].GetEnumNames().size() == 3 &&
        own[0].GetEnumNames()[0] == "directional" &&
        own[0].GetEnumNames()[1] == "point" &&
        own[0].GetEnumNames()[2] == "ambient";

    // 서술자를 통한 읽기·쓰기는 실제 접근자를 지난다. 음수 intensity가 0이 되는 것이 그 증거다:
    // 서술자는 필드가 아니라 setter를 부른다.
    Light light;
    const PropertyDescriptor* const intensity = FindProperty(Light::StaticType(), "intensity");
    bool roundTrips = intensity != nullptr &&
        intensity->TrySet(light, PropertyValue{ 2.5f }) &&
        light.GetIntensity() == 2.5f &&
        std::get<float>(intensity->Get(light)) == 2.5f;
    roundTrips = roundTrips &&
        intensity->TrySet(light, PropertyValue{ -5.0f }) && light.GetIntensity() == 0.0f;

    // 잘못된 종류의 값은 적용 대신 거부되고, 아무것도 바꾸지 않는다.
    const bool rejectsWrongKind = intensity != nullptr &&
        !intensity->TrySet(light, PropertyValue{ std::string("fast") }) &&
        light.GetIntensity() == 0.0f;

    // enum: 이름 표의 인덱스가 값이고, 표 밖의 값은 거부된다 — 장면 로더가 잘못된 문자열을
    // 거절하는 의미론이 여기서 나온다.
    const PropertyDescriptor* const kind = FindProperty(Light::StaticType(), "kind");
    const bool enumRoundTrips = kind != nullptr &&
        kind->TrySet(light, PropertyValue{ 1 }) &&
        light.GetKind() == Light::Kind::Point &&
        std::get<int>(kind->Get(light)) == 1;
    const bool enumRejectsOutOfRange = kind != nullptr &&
        !kind->TrySet(light, PropertyValue{ 3 }) && light.GetKind() == Light::Kind::Point;

    // 사슬 열람은 기반이 먼저다 — 직렬화가 쓰는 순서 — 그리고 이름 찾기는 파생에서도 기반의
    // 속성에 닿는다.
    ChainDerivedComponent derived;
    const std::vector<const PropertyDescriptor*> chain =
        CollectProperties(ChainDerivedComponent::StaticType());
    const bool chainIsBaseFirst = chain.size() == 2 &&
        chain[0]->GetName() == "baseValue" && chain[1]->GetName() == "label";
    const PropertyDescriptor* const inherited =
        FindProperty(ChainDerivedComponent::StaticType(), "baseValue");
    const bool findsInherited = inherited != nullptr &&
        inherited->TrySet(derived, PropertyValue{ 4.0f }) && derived.GetBaseValue() == 4.0f;
    const bool missingIsNull = FindProperty(Light::StaticType(), "missing") == nullptr;

    // 에셋 참조 속성은 어느 종류의 에셋을 원하는지를 함께 선언한다. 인스펙터가 그 종류만
    // 선택지로 보이므로, 빠진 선언은 사람이 모든 파일 사이에서 고르게 만든다.
    const auto declaredAssetType = [](const ComponentType& type, const char* const name)
        -> std::optional<GameEngine::Assets::AssetType>
    {
        const PropertyDescriptor* const property = FindProperty(type, name);
        return property ? property->GetAssetType() : std::nullopt;
    };
    using GameEngine::Assets::AssetType;
    const bool declaresAssetTypes =
        declaredAssetType(SpriteRenderer::StaticType(), "sprite") == AssetType::Sprite &&
        declaredAssetType(SpriteRenderer::StaticType(), "material") == AssetType::Material &&
        declaredAssetType(MeshRenderer::StaticType(), "mesh") == AssetType::Mesh &&
        declaredAssetType(AudioSource::StaticType(), "clip") == AssetType::AudioClip &&
        declaredAssetType(TilemapRenderer::StaticType(), "tileset") == AssetType::Sprite &&
        intensity != nullptr && !intensity->GetAssetType().has_value();

    // 다른 타입의 컴포넌트에 쓰인 서술자는 캐스팅 대신 검사에서 거부된다. 아래 줄은 의도된
    // 오류 로그 한 줄을 남긴다.
    const bool rejectsForeignComponent =
        intensity != nullptr && !intensity->TrySet(derived, PropertyValue{ 1.0f });

    // 생성 훅: 선언한 타입은 자기 인스턴스를 만들고, 선언하지 않은 타입은 만들 수 없다고 말한다.
    const std::unique_ptr<Component> created = Light::StaticType().CreateInstance();
    const bool creates = Light::StaticType().IsCreatable() &&
        created != nullptr && &created->GetComponentType() == &Light::StaticType();
    const bool abstractIsNotCreatable = !Component::StaticType().IsCreatable() &&
        Component::StaticType().CreateInstance() == nullptr;

    return Expect(declaresOwn, "a component should declare its own properties in order") &&
        Expect(declaresAssetTypes, "an asset reference property should declare its asset type") &&
        Expect(
            enumNamesMatchSceneFormat,
            "an enum property should carry the scene-format value names") &&
        Expect(roundTrips, "a property should read and write through the real accessors") &&
        Expect(rejectsWrongKind, "a value of the wrong kind should be rejected unchanged") &&
        Expect(enumRoundTrips, "an enum property should carry its value as a name-table index") &&
        Expect(enumRejectsOutOfRange, "an enum value outside the name table should be rejected") &&
        Expect(chainIsBaseFirst, "collected properties should list the base class first") &&
        Expect(findsInherited, "a name lookup should reach a base class property") &&
        Expect(missingIsNull, "an unknown property name should find nothing") &&
        Expect(
            rejectsForeignComponent,
            "a descriptor should refuse a component of another type") &&
        Expect(creates, "a creatable type should create an instance of itself") &&
        Expect(abstractIsNotCreatable, "a type without a factory should not be creatable");
}

namespace
{
    /// <summary>속성 값의 동등이다. Color에는 operator==가 없으므로 여기서 채널을 비교한다.</summary>
    bool PropertyValuesEqual(
        const GameEngine::Runtime::PropertyValue& left,
        const GameEngine::Runtime::PropertyValue& right)
    {
        if (left.index() != right.index())
        {
            return false;
        }
        return std::visit(
            [&right](const auto& value) -> bool
            {
                using Value = std::decay_t<decltype(value)>;
                const Value& other = std::get<Value>(right);
                if constexpr (std::is_same_v<Value, GameEngine::Math::Color>)
                {
                    return value.r == other.r && value.g == other.g &&
                        value.b == other.b && value.a == other.a;
                }
                else
                {
                    return value == other;
                }
            },
            left);
    }

    /// <summary>
    /// 왕복 검증을 위해 현재와 다른 값을 만든다. float의 x*0.5+0.25는 엔진 컴포넌트의 어떤
    /// 기본값에서 출발해도 실제 setter의 클램프 범위 안에 남으므로, 클램프에 잘려 왕복이 깨지는
    /// 거짓 실패가 없다.
    /// </summary>
    GameEngine::Runtime::PropertyValue MutatedPropertyValue(
        const GameEngine::Runtime::PropertyDescriptor& descriptor,
        const GameEngine::Runtime::PropertyValue& current)
    {
        using GameEngine::Runtime::PropertyKind;
        using GameEngine::Runtime::PropertyValue;
        const auto squeeze = [](const float value) { return value * 0.5f + 0.25f; };
        switch (descriptor.GetKind())
        {
        case PropertyKind::Bool:
            return PropertyValue{ !std::get<bool>(current) };
        case PropertyKind::Int:
            return PropertyValue{ std::get<int>(current) + 1 };
        case PropertyKind::Float:
            return PropertyValue{ squeeze(std::get<float>(current)) };
        case PropertyKind::String:
            return PropertyValue{ std::get<std::string>(current) + "*" };
        case PropertyKind::Vector2:
        {
            const auto& value = std::get<GameEngine::Math::Vector2>(current);
            return PropertyValue{
                GameEngine::Math::Vector2{ squeeze(value.GetX()), squeeze(value.GetY()) } };
        }
        case PropertyKind::Vector3:
        {
            const auto& value = std::get<GameEngine::Math::Vector3>(current);
            return PropertyValue{ GameEngine::Math::Vector3{
                squeeze(value.GetX()), squeeze(value.GetY()), squeeze(value.GetZ()) } };
        }
        case PropertyKind::Color:
        {
            const auto& value = std::get<GameEngine::Math::Color>(current);
            return PropertyValue{ GameEngine::Math::Color{
                squeeze(value.r), squeeze(value.g), squeeze(value.b), squeeze(value.a) } };
        }
        case PropertyKind::AssetReference:
            return PropertyValue{
                GameEngine::Assets::AssetReference::Parse("TestAssets/RoundTrip.png#1") };
        case PropertyKind::Enum:
            return PropertyValue{ (std::get<int>(current) + 1) %
                static_cast<int>(descriptor.GetEnumNames().size()) };
        }
        return current;
    }

    /// <summary>사슬의 모든 속성을 Get→변형→TrySet→Get으로 왕복시킨다.</summary>
    bool RoundTripProperties(
        const GameEngine::Runtime::ComponentType& type,
        GameEngine::Runtime::Component& component)
    {
        bool passed = true;
        for (const GameEngine::Runtime::PropertyDescriptor* const descriptor :
             GameEngine::Runtime::CollectProperties(type))
        {
            const GameEngine::Runtime::PropertyValue mutated =
                MutatedPropertyValue(*descriptor, descriptor->Get(component));
            if (!descriptor->TrySet(component, mutated) ||
                !PropertyValuesEqual(descriptor->Get(component), mutated))
            {
                std::cerr << "FAILED: a property did not round-trip through its accessors."
                             " type=" << type.GetName()
                          << ", property=" << descriptor->GetName() << '\n';
                passed = false;
            }
        }
        return passed;
    }

    bool EnumNamesAre(
        const GameEngine::Runtime::ComponentType& type, const std::string_view propertyName,
        const std::initializer_list<std::string_view> expected)
    {
        const GameEngine::Runtime::PropertyDescriptor* const descriptor =
            GameEngine::Runtime::FindProperty(type, propertyName);
        if (!descriptor || descriptor->GetKind() != GameEngine::Runtime::PropertyKind::Enum ||
            descriptor->GetEnumNames().size() != expected.size())
        {
            return false;
        }
        std::size_t index = 0;
        for (const std::string_view name : expected)
        {
            if (descriptor->GetEnumNames()[index++] != name)
            {
                return false;
            }
        }
        return true;
    }
}

/// <summary>
/// 엔진 컴포넌트 전체가 자기 속성을 서술로 선언하는지 확인한다. 이름·enum 문자열·선언 순서·
/// 생략 trait는 장면 파일 형식이므로, 서술을 읽는 일반 직렬화도 그 형식을 보존해야 한다.
/// </summary>
bool RunComponentPropertyDeclarationTests()
{
    using namespace GameEngine::Runtime;

    // 이름으로 만들 수 있는 모든 타입: 인스턴스를 만들고, 사슬의 모든 속성이 실제 접근자를
    // 지나 왕복한다.
    const ComponentType* const creatableTypes[] = {
        &Camera::StaticType(), &Light::StaticType(), &MeshRenderer::StaticType(),
        &SpriteRenderer::StaticType(), &TextRenderer::StaticType(), &AudioSource::StaticType(),
        &Rigidbody2D::StaticType(), &Rigidbody3D::StaticType(),
    };
    bool creatableRoundTrips = true;
    for (const ComponentType* const type : creatableTypes)
    {
        const std::unique_ptr<Component> instance = type->CreateInstance();
        if (!type->IsCreatable() || !instance)
        {
            std::cerr << "FAILED: a creatable type should create an instance. type="
                      << type->GetName() << '\n';
            creatableRoundTrips = false;
            continue;
        }
        creatableRoundTrips = RoundTripProperties(*type, *instance) && creatableRoundTrips;
    }

    // Transform은 객체의 일부라 이름으로 만들지 않지만, 속성은 같은 방식으로 왕복해야 한다.
    Transform transform;
    const bool transformRoundTrips = RoundTripProperties(Transform::StaticType(), transform);
    const bool transformIsNotCreatable = !Transform::StaticType().IsCreatable();

    // enum 이름 표는 열거자 순서대로의 장면 파일 문자열이다 — 팩토리가 받아들이는 문자열과 같다.
    const bool enumTablesMatchSceneFormat =
        EnumNamesAre(SpriteRenderer::StaticType(), "drawMode", { "simple", "sliced" }) &&
        EnumNamesAre(TextRenderer::StaticType(), "space", { "world", "screen" }) &&
        EnumNamesAre(TextRenderer::StaticType(), "alignment", { "left", "center", "right" }) &&
        EnumNamesAre(Light::StaticType(), "kind", { "directional", "point", "ambient" });

    // Space는 열거자 순서(World=0)와 직렬화 기본값("screen")이 어긋나는 케이스라, 표의 인덱스가
    // 실제 열거자로 가는지 따로 고정한다.
    TextRenderer text;
    const PropertyDescriptor* const space = FindProperty(TextRenderer::StaticType(), "space");
    const bool spaceMapsByEnumerator = space != nullptr &&
        space->TrySet(text, PropertyValue{ 0 }) &&
        text.GetSpace() == TextRenderer::Space::World &&
        space->TrySet(text, PropertyValue{ 1 }) &&
        text.GetSpace() == TextRenderer::Space::Screen;

    // 파생 렌더러의 사슬: Behaviour의 enabled가 맨 앞, Renderer의 공유 속성이 그 다음, 자기
    // 속성이 마지막 — 직렬화가 쓰는 순서다.
    const std::vector<const PropertyDescriptor*> chain =
        CollectProperties(MeshRenderer::StaticType());
    const char* const expectedChain[] = {
        "enabled", "visible", "castShadows", "receiveShadows", "sortingOrder", "material", "mesh",
        "sortWithSprites", "color",
    };
    bool chainIsBaseFirst = chain.size() == std::size(expectedChain);
    for (std::size_t index = 0; chainIsBaseFirst && index < chain.size(); ++index)
    {
        chainIsBaseFirst = chain[index]->GetName() == expectedChain[index];
    }

    // 무효한 값을 파일에서 생략하는지도 형식의 일부다: material은 유효할 때만 쓰이고, clip은
    // 빈 참조도 항상 쓰인다.
    const PropertyDescriptor* const material = FindProperty(MeshRenderer::StaticType(), "material");
    const PropertyDescriptor* const clip = FindProperty(AudioSource::StaticType(), "clip");
    const bool traitsMatchSceneFormat = material != nullptr && clip != nullptr &&
        HasTrait(material->GetTraits(), PropertyTraits::OmitWhenInvalid) &&
        !HasTrait(clip->GetTraits(), PropertyTraits::OmitWhenInvalid);

    // Camera의 orthographic: 파일의 표현은 bool이고, 접근자가 런타임의 ProjectionMode로
    // 변환한다.
    Camera camera;
    const PropertyDescriptor* const orthographic =
        FindProperty(Camera::StaticType(), "orthographic");
    const bool orthographicMapsToProjectionMode = orthographic != nullptr &&
        orthographic->GetKind() == PropertyKind::Bool &&
        orthographic->TrySet(camera, PropertyValue{ true }) &&
        camera.GetProjectionMode() == Camera::ProjectionMode::Orthographic &&
        std::get<bool>(orthographic->Get(camera)) &&
        orthographic->TrySet(camera, PropertyValue{ false }) &&
        camera.GetProjectionMode() == Camera::ProjectionMode::Perspective;

    return Expect(creatableRoundTrips, "every creatable type's properties should round-trip") &&
        Expect(transformRoundTrips, "Transform's properties should round-trip") &&
        Expect(transformIsNotCreatable, "Transform should not be creatable by name") &&
        Expect(
            enumTablesMatchSceneFormat,
            "enum name tables should carry the scene-format strings in enumerator order") &&
        Expect(spaceMapsByEnumerator, "an enum name-table index should map to its enumerator") &&
        Expect(
            chainIsBaseFirst,
            "a derived renderer should list enabled, the shared properties, then its own") &&
        Expect(
            traitsMatchSceneFormat,
            "omit-when-invalid traits should match what today's writer omits") &&
        Expect(
            orthographicMapsToProjectionMode,
            "Camera's orthographic bool should map to ProjectionMode");
}

/// <summary>
/// 기본 Clone이 속성 서술로 복제하는 것을 고정한다: 만들 수 있는 모든 타입이 값을 바꾼 뒤에도
/// 손으로 쓴 Clone 없이 GameObject 복제를 지나 같은 값으로 돌아오고 — enabled도 속성이므로
/// 함께 온다 — Transform의 override와 복제할 수 없는 컴포넌트의 건너뜀 의미론은 그대로다.
/// </summary>
bool RunComponentCloneTests()
{
    using namespace GameEngine::Runtime;

    ObjectRegistry registry;
    const Input input;
    RuntimeContext runtimeContext(registry, input);
    auto original = std::make_unique<GameObject>(runtimeContext, "Original");

    const ComponentType* const creatableTypes[] = {
        &Camera::StaticType(), &Light::StaticType(), &MeshRenderer::StaticType(),
        &SpriteRenderer::StaticType(), &TextRenderer::StaticType(), &AudioSource::StaticType(),
        &Rigidbody2D::StaticType(), &Rigidbody3D::StaticType(),
    };
    bool mutated = true;
    for (const ComponentType* const type : creatableTypes)
    {
        Component* const component = original->AddComponent(type->CreateInstance());
        mutated = component && RoundTripProperties(*type, *component) && mutated;
    }
    mutated = RoundTripProperties(Transform::StaticType(), original->GetTransform()) && mutated;

    // 복제를 지원하지 않는 컴포넌트는 경고와 함께 빠지고 복제를 막지 않는다. 아래 복제는 의도된
    // 경고 로그 한 줄을 남긴다.
    ChainDerivedComponent* const unclonable = original->AddComponent<ChainDerivedComponent>();

    const std::unique_ptr<GameObject> clone = original->Clone();
    bool cloneMatches = clone != nullptr;
    if (cloneMatches)
    {
        for (const ComponentType* const type : creatableTypes)
        {
            const Component* originalComponent = nullptr;
            const Component* clonedComponent = nullptr;
            for (const std::unique_ptr<Component>& candidate : original->GetAllComponents())
            {
                if (&candidate->GetComponentType() == type)
                {
                    originalComponent = candidate.get();
                }
            }
            for (const std::unique_ptr<Component>& candidate : clone->GetAllComponents())
            {
                if (&candidate->GetComponentType() == type)
                {
                    clonedComponent = candidate.get();
                }
            }
            if (!originalComponent || !clonedComponent)
            {
                std::cerr << "FAILED: a cloned object should carry every creatable component."
                             " type=" << type->GetName() << '\n';
                cloneMatches = false;
                continue;
            }
            for (const PropertyDescriptor* const descriptor : CollectProperties(*type))
            {
                if (!PropertyValuesEqual(
                        descriptor->Get(*originalComponent), descriptor->Get(*clonedComponent)))
                {
                    std::cerr << "FAILED: a property came back different after cloning. type="
                              << type->GetName() << ", property=" << descriptor->GetName() << '\n';
                    cloneMatches = false;
                }
            }
        }
    }

    // Transform은 생성 훅이 없어 override가 남고, GameObject::Clone이 값을 옮긴다.
    bool transformMatches = clone != nullptr;
    for (const PropertyDescriptor* const descriptor :
         CollectProperties(Transform::StaticType()))
    {
        transformMatches = transformMatches && PropertyValuesEqual(
            descriptor->Get(original->GetTransform()), descriptor->Get(clone->GetTransform()));
    }

    const bool unclonableSkipped = unclonable != nullptr && clone != nullptr &&
        clone->GetComponent<ChainDerivedComponent>() == nullptr;

    return Expect(mutated, "the original's properties should mutate before cloning") &&
        Expect(cloneMatches, "a clone should carry every declared property value") &&
        Expect(transformMatches, "the transform override should still clone its values") &&
        Expect(
            unclonableSkipped,
            "a component that cannot clone should be skipped, not block the clone");
}

/// <summary>
/// 타일맵의 격자 산술을 고정한다: 칸이 배열의 어디에 놓이고, 로컬 좌표가 어느 칸이며, 격자
/// 크기를 바꿔도 남은 칸이 제자리를 지키는지.
/// </summary>
bool RunTilemapTests()
{
    using GameEngine::Runtime::TilemapRenderer;

    TilemapRenderer tilemap;
    tilemap.SetColumns(4);
    tilemap.SetRows(3);
    tilemap.SetCellSize({ 2.0f, 1.0f });

    // 배열은 행 우선이다: 행 1의 첫 칸은 열 수만큼 뒤에 있다.
    const bool indexIsRowMajor = tilemap.GetTileArrayIndex(0, 0) == 0 &&
        tilemap.GetTileArrayIndex(3, 0) == 3 &&
        tilemap.GetTileArrayIndex(0, 1) == 4 &&
        tilemap.GetTileArrayIndex(3, 2) == 11;

    // 격자 밖은 자리가 없다 — 페인팅이 그 사실로 드래그를 흘려보낸다.
    const bool rejectsOutside = tilemap.GetTileArrayIndex(4, 0) == -1 &&
        tilemap.GetTileArrayIndex(0, 3) == -1 &&
        tilemap.GetTileArrayIndex(-1, 0) == -1 &&
        tilemap.GetTile(9, 9) == TilemapRenderer::EmptyTile;

    // 새 타일맵의 칸은 모두 비어 있고, 같은 값을 다시 쓰면 바뀐 것이 없다고 답한다 — 페인팅이
    // 한 획에서 같은 칸을 두 번 기록하지 않는 근거다.
    const bool startsEmpty = tilemap.GetTile(2, 1) == TilemapRenderer::EmptyTile;
    const bool reportsChange = tilemap.SetTile(2, 1, 7) && !tilemap.SetTile(2, 1, 7) &&
        tilemap.GetTile(2, 1) == 7;

    // 칸 가운데는 칸 크기의 절반만큼 원점에서 떨어져 있고, 좌표는 그 반대로 칸을 찾는다.
    const GameEngine::Math::Vector2 center = tilemap.GetCellCenter(2, 1);
    int column = 0;
    int row = 0;
    const bool centerRoundTrips = center.GetX() == 5.0f && center.GetY() == 1.5f &&
        tilemap.TryGetCellAt(center.GetX(), center.GetY(), column, row) &&
        column == 2 && row == 1;

    // 원점 왼쪽·아래는 음수 칸이라 격자 밖이다. 0 쪽으로 자르는 나눗셈이었다면 -0.5칸을 0칸
    // 이라고 답했을 자리다.
    const bool negativeIsOutside = !tilemap.TryGetCellAt(-0.5f, 0.5f, column, row) &&
        column == -1 && !tilemap.TryGetCellAt(1.0f, -0.5f, column, row) && row == -1;

    // 격자를 넓히면 남은 칸은 자리를 지킨다 — 행 우선 자리는 열 수에 달렸으므로, 열을 늘리면
    // 배열이 다시 잡히면서 예전 칸의 값이 다른 칸으로 옮겨 보일 수 있다. 그 사실을 고정한다:
    // 크기 변경은 배열 크기만 맞추고 내용을 옮기지 않는다.
    tilemap.SetRows(4);
    const bool growKeepsArrayOrder = tilemap.GetTile(2, 1) == 7 &&
        static_cast<int>(tilemap.GetTiles().size()) == 16 &&
        tilemap.GetTile(0, 3) == TilemapRenderer::EmptyTile;

    // 통째로 넣은 배열은 격자 크기에 맞춰진다: 모자라면 빈 칸으로 채우고 넘치면 자른다.
    tilemap.SetTiles({ 1, 2, 3 });
    const bool setTilesFits = static_cast<int>(tilemap.GetTiles().size()) == 16 &&
        tilemap.GetTile(0, 0) == 1 && tilemap.GetTile(2, 0) == 3 &&
        tilemap.GetTile(3, 0) == TilemapRenderer::EmptyTile;

    return Expect(indexIsRowMajor, "tile array indices should run row by row") &&
        Expect(rejectsOutside, "a cell outside the grid should have no array slot") &&
        Expect(startsEmpty, "a new tilemap should start with empty cells") &&
        Expect(reportsChange, "setting a cell should report whether it changed") &&
        Expect(centerRoundTrips, "a cell centre should map back to its own cell") &&
        Expect(negativeIsOutside, "coordinates below the origin should fall outside the grid") &&
        Expect(growKeepsArrayOrder, "resizing should keep the array and fill new cells empty") &&
        Expect(setTilesFits, "a replaced tile array should be fitted to the grid");
}

/// <summary>
/// 애니메이션이 시간에서 프레임을 고르는 규칙을 고정한다. 프레임을 하나씩 세는 대신 흐른
/// 시간으로 계산하므로, 한 프레임이 길게 걸려 여러 장을 건너뛰어야 할 때도 시계와 어긋나지
/// 않는다 — 그 성질이 여기서 지켜진다.
/// </summary>
bool RunSpriteAnimationTests()
{
    using namespace GameEngine::Runtime;

    // 10fps 클립: 0.05초는 아직 첫 장, 0.15초는 둘째 장이다.
    const bool advancesWithTime =
        SelectAnimationFrame(0.0f, 0, 4, 10.0f, true) == 0 &&
        SelectAnimationFrame(0.05f, 0, 4, 10.0f, true) == 0 &&
        SelectAnimationFrame(0.15f, 0, 4, 10.0f, true) == 1 &&
        SelectAnimationFrame(0.35f, 0, 4, 10.0f, true) == 3;

    // 프레임 드랍: 한 번에 0.5초가 흘러도 그 시각의 장이 나온다 — 한 장씩 세었다면 둘째 장에
    // 머물렀을 자리다.
    const bool survivesFrameDrops = SelectAnimationFrame(0.5f, 0, 4, 10.0f, true) == 1 &&
        SelectAnimationFrame(2.25f, 0, 4, 10.0f, true) == 2;

    // 클립의 첫 프레임은 시트 안의 자리다: 구간이 옮겨져도 같은 순서로 돈다.
    const bool respectsFirstFrame = SelectAnimationFrame(0.25f, 8, 4, 10.0f, true) == 10;

    // loop가 아니면 마지막 장에 멈춘다.
    const bool clampsWithoutLoop = SelectAnimationFrame(10.0f, 0, 4, 10.0f, false) == 3 &&
        SelectAnimationFrame(0.15f, 0, 4, 10.0f, false) == 1;

    // 멈춘 클립과 재생 속도 0은 첫 장이다.
    const bool stillWhenStopped = SelectAnimationFrame(5.0f, 2, 4, 0.0f, true) == 2 &&
        SelectAnimationFrame(-1.0f, 2, 4, 10.0f, true) == 2;

    // 프레임 수를 말하지 않은 클립은 번호를 감싸지 않고 늘려 보낸다 — 감싸기는 시트가 한다.
    const bool openEndedCounts = SelectAnimationFrame(0.35f, 0, 0, 10.0f, true) == 3 &&
        SelectAnimationFrame(1.05f, 0, 0, 10.0f, true) == 10;

    // 컴포넌트는 같은 객체의 스프라이트 렌더러에 그 답을 쓴다.
    ObjectRegistry registry;
    const Input input;
    RuntimeContext runtimeContext(registry, input);
    GameObject object(runtimeContext, "Animated");
    SpriteRenderer* const renderer = object.AddComponent<SpriteRenderer>();
    SpriteAnimator* const animator = object.AddComponent<SpriteAnimator>();
    bool drivesRenderer = renderer != nullptr && animator != nullptr;
    if (drivesRenderer)
    {
        animator->SetFirstFrame(0);
        animator->SetFrameCount(4);
        animator->SetFrameRate(10.0f);
        object.Update(0.25f);
        drivesRenderer = renderer->GetFrame() == 2;
        object.Update(0.1f);
        drivesRenderer = drivesRenderer && renderer->GetFrame() == 3;
        // 멈추면 시간이 서고, 그 자리의 장에 머문다.
        animator->SetPlaying(false);
        object.Update(1.0f);
        drivesRenderer = drivesRenderer && renderer->GetFrame() == 3;
    }

    // 시트는 프레임을 좌상단부터 행 우선으로 센다. 2x2 시트의 세 번째 프레임은 아래 왼쪽이다.
    const GameEngine::Assets::Sprite sheetSprite(
        1, "Sheet.png", "Sheet.png", 0, 0, 100.0f, {}, { 2, 2, 0, 12.0f });
    float u = 0.0f;
    float v = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    sheetSprite.GetFrameRect(2, u, v, width, height);
    const bool framesAreRowMajor = width == 0.5f && height == 0.5f && u == 0.0f && v == 0.5f;

    // 범위를 넘는 번호는 감싸이므로, 애니메이션이 프레임을 넘겨도 빈 그림이 되지 않는다.
    sheetSprite.GetFrameRect(5, u, v, width, height);
    const bool wrapsOutOfRange = u == 0.5f && v == 0.0f;

    // 시트를 말하지 않은 스프라이트는 이미지 한 장이 곧 한 프레임이다.
    const GameEngine::Assets::Sprite plainSprite(2, "Plain.png", "Plain.png", 0, 0);
    plainSprite.GetFrameRect(3, u, v, width, height);
    const bool plainIsWholeImage = u == 0.0f && v == 0.0f && width == 1.0f && height == 1.0f;

    return Expect(advancesWithTime, "a clip should show the frame its elapsed time names") &&
        Expect(survivesFrameDrops, "a long frame should land on the right frame, not the next one") &&
        Expect(respectsFirstFrame, "a clip should start counting at its first frame") &&
        Expect(clampsWithoutLoop, "a clip that does not loop should hold its last frame") &&
        Expect(stillWhenStopped, "a stopped clip should stay on its first frame") &&
        Expect(openEndedCounts, "a clip without a frame count should keep counting up") &&
        Expect(drivesRenderer, "an animator should write its frame to the sprite renderer") &&
        Expect(framesAreRowMajor, "sheet frames should run left to right, top to bottom") &&
        Expect(wrapsOutOfRange, "a frame past the end should wrap into the sheet") &&
        Expect(plainIsWholeImage, "an image without a sheet should be one whole frame");
}

/// <summary>
/// 월드 행렬 캐시의 계약을 고정한다: 캐시는 관찰 가능한 동작이 아니므로 — 값은 캐시가 없을
/// 때와 언제나 같아야 한다 — 여기서는 무효화가 빠지기 쉬운 길들을 지나며 값이 옳은지 본다.
/// 로컬 값 변경, 부모의 변경이 자손에 닿는 것, 재부모화, 월드 유지 재부모화, 부모의 파괴다.
/// </summary>
bool RunTransformWorldMatrixTests()
{
    using namespace GameEngine::Runtime;
    using GameEngine::Math::Vector3;

    const auto isNear = [](const Vector3& value, const Vector3& expected)
    {
        return std::abs(value.GetX() - expected.GetX()) < 1e-4f &&
            std::abs(value.GetY() - expected.GetY()) < 1e-4f &&
            std::abs(value.GetZ() - expected.GetZ()) < 1e-4f;
    };

    // 값 변경은 캐시된 답을 갈아치운다 — 위치, 크기 각각.
    Transform lone;
    lone.SetPosition({ 1.0f, 2.0f, 3.0f });
    const bool caches = isNear(lone.GetLocalToWorldMatrix().GetTranslation(), { 1.0f, 2.0f, 3.0f });
    lone.SetPosition({ 4.0f, 5.0f, 6.0f });
    lone.SetScale({ 2.0f, 2.0f, 2.0f });
    const bool recomputesAfterSet =
        isNear(lone.GetLocalToWorldMatrix().GetTranslation(), { 4.0f, 5.0f, 6.0f }) &&
        isNear(lone.GetLocalToWorldMatrix().TransformPoint({ 1.0f, 0.0f, 0.0f }),
               { 6.0f, 5.0f, 6.0f });

    // 부모의 변경은 이미 계산해 둔 자식·손자의 답까지 무효화하고, 형제의 답은 그대로 옳다.
    Transform parent;
    Transform child;
    Transform grandchild;
    Transform sibling;
    parent.SetPosition({ 1.0f, 0.0f, 0.0f });
    child.SetPosition({ 0.0f, 1.0f, 0.0f });
    grandchild.SetPosition({ 0.0f, 0.0f, 1.0f });
    sibling.SetPosition({ 0.0f, 0.0f, 5.0f });
    bool hierarchy = child.SetParent(&parent) && grandchild.SetParent(&child) &&
        sibling.SetParent(&parent);
    hierarchy = hierarchy &&
        isNear(grandchild.GetLocalToWorldMatrix().GetTranslation(), { 1.0f, 1.0f, 1.0f });
    parent.SetPosition({ 5.0f, 0.0f, 0.0f });
    const bool parentChangeReachesDescendants = hierarchy &&
        isNear(child.GetLocalToWorldMatrix().GetTranslation(), { 5.0f, 1.0f, 0.0f }) &&
        isNear(grandchild.GetLocalToWorldMatrix().GetTranslation(), { 5.0f, 1.0f, 1.0f });
    const bool siblingStaysCorrect = hierarchy &&
        isNear(sibling.GetLocalToWorldMatrix().GetTranslation(), { 5.0f, 0.0f, 5.0f });

    // 재부모화: 로컬을 유지하면 새 사슬 위의 값이 되고, 월드를 유지하면 답이 그대로다.
    const bool unparented = child.SetParent(nullptr) &&
        isNear(child.GetLocalToWorldMatrix().GetTranslation(), { 0.0f, 1.0f, 0.0f }) &&
        isNear(grandchild.GetLocalToWorldMatrix().GetTranslation(), { 0.0f, 1.0f, 1.0f });
    const Vector3 before = grandchild.GetLocalToWorldMatrix().GetTranslation();
    const bool worldKept = grandchild.SetParent(&parent, true) &&
        isNear(grandchild.GetLocalToWorldMatrix().GetTranslation(), before);

    // 물리처럼 월드 좌표에서 움직이는 호출자는 부모의 로컬 좌표를 추측하지 않는다. 부모의 이동과
    // 크기를 되돌려 원하는 월드 위치를 쓴다. 역행렬이 없는 부모는 위치를 망가뜨리지 않고 거절한다.
    Transform movingParent;
    Transform movingChild;
    movingParent.SetPosition({ 5.0f, 0.0f, 0.0f });
    movingParent.SetScale({ 2.0f, 2.0f, 1.0f });
    const bool setsWorldPosition = movingChild.SetParent(&movingParent) &&
        movingChild.SetWorldPosition({ 9.0f, 5.0f, 0.0f }) &&
        isNear(movingChild.GetWorldPosition(), { 9.0f, 5.0f, 0.0f });
    Transform singularParent;
    Transform singularChild;
    singularParent.SetScale({ 0.0f, 1.0f, 1.0f });
    singularChild.SetPosition({ 2.0f, 3.0f, 0.0f });
    const bool rejectsSingularParent = singularChild.SetParent(&singularParent) &&
        !singularChild.SetWorldPosition({ 7.0f, 8.0f, 0.0f }) &&
        isNear(singularChild.GetPosition(), { 2.0f, 3.0f, 0.0f });

    // 부모가 파괴되면 자식의 월드는 자기 로컬로 돌아간다 — 캐시가 옛 부모를 기억하면 안 된다.
    Transform orphan;
    orphan.SetPosition({ 0.0f, 2.0f, 0.0f });
    bool orphanFallsBack = true;
    {
        Transform dying;
        dying.SetPosition({ 7.0f, 0.0f, 0.0f });
        orphanFallsBack = orphan.SetParent(&dying) &&
            isNear(orphan.GetLocalToWorldMatrix().GetTranslation(), { 7.0f, 2.0f, 0.0f });
    }
    orphanFallsBack = orphanFallsBack &&
        isNear(orphan.GetLocalToWorldMatrix().GetTranslation(), { 0.0f, 2.0f, 0.0f });

    return Expect(caches, "a transform should answer its world matrix") &&
        Expect(recomputesAfterSet, "changing local values should recompute the world matrix") &&
        Expect(
            parentChangeReachesDescendants,
            "a parent's change should reach already-computed descendants") &&
        Expect(siblingStaysCorrect, "a sibling should stay correct through others' changes") &&
        Expect(unparented, "unparenting should drop the old chain from the answer") &&
        Expect(worldKept, "world-preserving reparenting should keep the world position") &&
        Expect(setsWorldPosition, "a child should accept a world-space position under its parent") &&
        Expect(rejectsSingularParent, "a child should keep its local position under a singular parent") &&
        Expect(orphanFallsBack, "a destroyed parent should not linger in a child's cache");
}

bool RunComponentStateContractTests()
{
    const TestSupport::RegistryScope registries;
    using namespace GameEngine::Runtime;
    using GameEngine::Core::Json;
    using GameEngine::Serialization::SceneSerializer;

    // "컴포넌트의 상태 = 선언된 속성 + 속성 밖 상태"라는 계약이 실제로 지켜지는지 보는 시험이다.
    // 세 경로 — 복제, undo 스냅숏, 저장 — 가 같은 정의를 통과하는지를 "직렬화(원본) ==
    // 직렬화(복원본)"이라는 한 문장으로 묻는다. 타일이 채워진 타일맵으로 물어야 한다: 빈 격자는
    // 상태를 통째로 빠뜨려도 같은 텍스트가 나와서 아무것도 잡지 못한다.
    std::vector<const ComponentType*> borrowed;
    for (const ComponentType* const type :
         { &Transform::StaticType(), &TilemapRenderer::StaticType(),
           &SpriteRenderer::StaticType() })
    {
        // 등록 표는 프로세스 전역이고 다른 스위트가 자기 등록의 성공을 단언하므로, 빌린 것만
        // 돌려준다.
        if (GameEngine::Serialization::RegisterComponentType(*type))
        {
            borrowed.push_back(type);
        }
    }

    ObjectRegistry registry;
    const Input input;
    RuntimeContext runtimeContext(registry, input);

    const auto serialize = [](const Component& component)
    {
        const std::optional<Json> json = SceneSerializer::SaveComponentToJson(component);
        return json ? json->Dump() : std::string{};
    };

    auto source = std::make_unique<GameObject>(runtimeContext, "Map");
    source->GetTransform().SetPosition({ 1.5f, -2.0f, 0.25f });
    source->GetTransform().SetScale({ 2.0f, 2.0f, 1.0f });
    TilemapRenderer* const map = source->AddComponent<TilemapRenderer>();
    bool built = map != nullptr;
    if (built)
    {
        map->SetColumns(3);
        map->SetRows(2);
        map->SetCellSize({ 0.5f, 0.25f });
        map->SetTileset(GameEngine::Assets::AssetReference::Parse("Textures/Tiles.png"));
        built = map->SetTile(0, 0, 4) && map->SetTile(2, 1, 9);
    }

    // 격자가 실제로 직렬화에 실려 있는지부터 확인한다. 이것이 아니면 아래의 비교들은 "빈 것과
    // 빈 것이 같다"는 공허한 참이 될 수 있다.
    const std::string sourceText = built ? serialize(*map) : std::string{};
    const bool statePresent = sourceText.find("\"layers\"") != std::string::npos &&
        sourceText.find('4') != std::string::npos && sourceText.find('9') != std::string::npos;

    // 복제는 속성뿐 아니라 타일맵 격자도 보존해 Play 진입과 Instantiate에서 같은 타일을 구성해야 한다.
    const std::unique_ptr<GameObject> clone = source->Clone();
    const TilemapRenderer* const clonedMap =
        clone ? clone->GetComponent<TilemapRenderer>() : nullptr;
    const bool cloneKeepsState = clonedMap && serialize(*clonedMap) == sourceText &&
        clonedMap->GetTile(0, 0) == 4 && clonedMap->GetTile(2, 1) == 9;

    // ② undo 스냅숏: 에디터가 컴포넌트를 뜨고 되세우는 경로 그대로다. 스냅숏이 곧 컴포넌트
    // JSON이고, 복원은 장면 로드와 같은 길이다.
    const std::optional<Json> snapshot =
        built ? SceneSerializer::SaveComponentToJson(*map) : std::nullopt;
    auto restoredObject = std::make_unique<GameObject>(runtimeContext, "Restored");
    Component* const restored = snapshot
        ? SceneSerializer::LoadComponentIntoGameObject(*snapshot, *restoredObject)
        : nullptr;
    const TilemapRenderer* const restoredMap =
        restoredObject->GetComponent<TilemapRenderer>();
    const bool snapshotKeepsState = restored && restoredMap && restored == restoredMap &&
        serialize(*restoredMap) == sourceText && restoredMap->GetTile(2, 1) == 9;

    // Transform은 객체의 일부라 새로 붙지 않는다. 그래도 복원은 채워진 그 Transform을 돌려줘야
    // 한다 — 에디터가 옛 id의 별칭을 그 반환값으로 잇기 때문이다.
    const std::optional<Json> transformSnapshot =
        SceneSerializer::SaveComponentToJson(source->GetTransform());
    Component* const restoredTransform = transformSnapshot
        ? SceneSerializer::LoadComponentIntoGameObject(*transformSnapshot, *restoredObject)
        : nullptr;
    const bool transformRestores = restoredTransform &&
        restoredTransform == &restoredObject->GetTransform() &&
        serialize(*restoredTransform) == serialize(source->GetTransform());

    // ③ 이 판이 그리지 못하는 뒤의 레이어. 읽지 않는다는 것이 잃어도 된다는 뜻이 되면, 다층
    // 장면을 이 판의 에디터로 한 번 열어 저장하는 것만으로 둘째 레이어가 사라진다.
    const Json multiLayer = Json::Parse(
        R"({"type": "TilemapRenderer", "columns": 2, "rows": 1, )"
        R"("layers": [{"tiles": [5, 6]}, {"name": "decor", "tiles": [7, 8]}]})");
    auto multiObject = std::make_unique<GameObject>(runtimeContext, "MultiLayer");
    Component* const multiMap =
        SceneSerializer::LoadComponentIntoGameObject(multiLayer, *multiObject);
    const std::optional<Json> rewritten =
        multiMap ? SceneSerializer::SaveComponentToJson(*multiMap) : std::nullopt;
    const Json* const rewrittenLayers = rewritten ? rewritten->Find("layers") : nullptr;
    const bool keepsUnreadLayers = rewrittenLayers && rewrittenLayers->IsArray() &&
        rewrittenLayers->Size() == 2 &&
        rewrittenLayers->At(1).Dump() == multiLayer.At("layers").At(1).Dump() &&
        rewrittenLayers->At(0).At("tiles").At(0).Get<int>() == 5;

    // 보존한 레이어도 상태다: 복제와 스냅숏 왕복이 그것까지 옮겨야 계약이 닫힌다.
    const std::unique_ptr<GameObject> multiClone = multiObject->Clone();
    const TilemapRenderer* const clonedMulti =
        multiClone ? multiClone->GetComponent<TilemapRenderer>() : nullptr;
    const bool unreadLayersSurviveClone = clonedMulti && multiMap &&
        serialize(*clonedMulti) == serialize(*multiMap);

    for (const ComponentType* const type : borrowed)
    {
        static_cast<void>(
            GameEngine::Serialization::ComponentFactory::Unregister(type->GetName()));
    }

    return Expect(built, "a tilemap should accept the painted tiles this test compares") &&
        Expect(statePresent, "a filled tilemap should carry its grid into its serialized form") &&
        Expect(cloneKeepsState, "a clone should serialize the same as the component it copied") &&
        Expect(
            snapshotKeepsState,
            "a component rebuilt from its snapshot should serialize the same as the original") &&
        Expect(
            transformRestores,
            "restoring a Transform snapshot should fill and return the object's own Transform") &&
        Expect(
            keepsUnreadLayers, "a tilemap should write back the layers it does not read") &&
        Expect(
            unreadLayersSurviveClone, "a clone should carry the layers the tilemap cannot read");
}

bool RunComponentSchemaTests()
{
    const TestSupport::RegistryScope registries;
    using namespace GameEngine::Runtime;
    using GameEngine::Core::Json;
    namespace Serialization = GameEngine::Serialization;

    // 게임 실행 파일이 적어 두는 것과 에디터가 읽는 것이 같은 것인지가 이 시험의 전부다. 두
    // 쪽은 서로 다른 프로세스라, 어긋나면 컴파일이 아니라 사람이 에디터에서 발견하게 된다.
    const ComponentType* const types[] = {
        &SpriteRenderer::StaticType(), &TextRenderer::StaticType(), &Transform::StaticType(),
    };
    const std::string text = Serialization::WriteComponentSchemas(types);
    const std::vector<Serialization::ComponentSchema> schemas =
        Serialization::ParseComponentSchemas(text);

    // Transform은 만들 수 없는 타입이라 목록에서 빠진다: 에디터가 붙일 수 없는 것을 목록에
    // 올리면 누를 수 있는 것이 아무 일도 하지 않는다.
    const auto findSchema = [&schemas](const std::string_view name)
        -> const Serialization::ComponentSchema*
    {
        for (const Serialization::ComponentSchema& schema : schemas)
        {
            if (schema.typeName == name)
            {
                return &schema;
            }
        }
        return nullptr;
    };
    const Serialization::ComponentSchema* const sprite = findSchema("SpriteRenderer");
    const bool skipsUncreatable = findSchema("Transform") == nullptr;
    const bool foundTypes = sprite != nullptr && findSchema("TextRenderer") != nullptr;

    // 속성 표가 서술 그대로 건너왔는지: 이름·종류·순서, enum 이름 표, 그리고 기본값.
    bool describesProperties = false;
    bool carriesEnumNames = false;
    bool carriesAssetType = false;
    bool carriesDefaults = false;
    if (sprite)
    {
        const std::vector<const PropertyDescriptor*> descriptors =
            CollectProperties(SpriteRenderer::StaticType());
        describesProperties = descriptors.size() == sprite->properties.size();
        for (std::size_t index = 0; describesProperties && index < descriptors.size(); ++index)
        {
            describesProperties = descriptors[index]->GetName() == sprite->properties[index].name &&
                descriptors[index]->GetKind() == sprite->properties[index].kind &&
                descriptors[index]->GetDisplayName() == sprite->properties[index].displayName;
        }
        for (const Serialization::ComponentSchemaProperty& property : sprite->properties)
        {
            if (property.name == "sprite")
            {
                carriesAssetType = property.kind == PropertyKind::AssetReference &&
                    property.assetType == GameEngine::Assets::AssetType::Sprite;
            }
            if (property.name == "drawMode")
            {
                carriesEnumNames = property.kind == PropertyKind::Enum &&
                    property.enumNames.size() == 2 && property.enumNames[0] == "simple";
            }
            if (property.name == "size")
            {
                const SpriteRenderer defaults;
                carriesDefaults = property.defaultValue.Dump() ==
                    Serialization::PropertyValueToJson(
                        PropertyKind::Vector2, PropertyValue{ defaults.GetSize() }, {}).Dump();
            }
        }
    }

    // 스키마의 기본값으로 만든 컴포넌트는 그 타입을 아는 프로세스가 기본 생성한 것과 같은
    // 파일이 되어야 한다. 에디터가 붙인 컴포넌트를 게임이 열었을 때 다른 값이 되지 않는다는
    // 뜻이고, 그것이 이 왕복이 지키는 약속이다.
    bool defaultsMatchInstance = false;
    if (sprite)
    {
        const bool registered =
            Serialization::RegisterComponentType(SpriteRenderer::StaticType());
        ObjectRegistry registry;
        const Input input;
        RuntimeContext runtimeContext(registry, input);
        auto gameObject = std::make_unique<GameObject>(runtimeContext, "FromSchema");
        const Component* const created =
            Serialization::SceneSerializer::LoadComponentIntoGameObject(
                Serialization::MakeDefaultComponentJson(*sprite), *gameObject);
        const SpriteRenderer defaults;
        const std::optional<Json> fromSchema =
            created ? Serialization::SceneSerializer::SaveComponentToJson(*created) : std::nullopt;
        const std::optional<Json> fromInstance =
            Serialization::SceneSerializer::SaveComponentToJson(defaults);
        defaultsMatchInstance = fromSchema && fromInstance &&
            fromSchema->Dump() == fromInstance->Dump();
        if (registered)
        {
        }
    }

    // 읽을 수 없는 파일은 빈 목록이다 — 그리고 그 사실을 로그가 말한다. 아래 두 줄은 의도된
    // 오류 로그를 남긴다.
    const bool rejectsGarbage = Serialization::ParseComponentSchemas("not json").empty();
    const bool rejectsOtherVersion =
        Serialization::ParseComponentSchemas(R"({"schemaVersion": 99, "components": []})").empty();

    // 값과 파일 표현 사이의 왕복. enum은 인덱스가 아니라 이름으로 오간다.
    const std::vector<std::string> enumNames{ "world", "screen" };
    const bool valuesRoundTrip =
        std::get<float>(Serialization::PropertyValueFromJson(
            PropertyKind::Float,
            Serialization::PropertyValueToJson(PropertyKind::Float, PropertyValue{ 2.5f }, {}), {},
            PropertyValue{ 0.0f })) == 2.5f &&
        std::get<GameEngine::Math::Vector2>(Serialization::PropertyValueFromJson(
            PropertyKind::Vector2,
            Serialization::PropertyValueToJson(
                PropertyKind::Vector2, PropertyValue{ GameEngine::Math::Vector2{ 3.0f, 4.0f } }, {}),
            {}, PropertyValue{ GameEngine::Math::Vector2{} })) ==
            GameEngine::Math::Vector2{ 3.0f, 4.0f } &&
        Serialization::PropertyValueToJson(PropertyKind::Enum, PropertyValue{ 1 }, enumNames)
                .Get<std::string>() == "screen" &&
        std::get<int>(Serialization::PropertyValueFromJson(
            PropertyKind::Enum, Json(std::string("screen")), enumNames, PropertyValue{ 0 })) == 1;

    // 형태가 맞지 않는 값은 로더처럼 조용히 기본값으로 남는다.
    const bool keepsFallback = std::get<float>(Serialization::PropertyValueFromJson(
        PropertyKind::Float, Json(std::string("not a number")), {}, PropertyValue{ 7.0f })) == 7.0f;

    return Expect(foundTypes, "a schema should describe the types it was written for") &&
        Expect(skipsUncreatable, "a type the editor cannot create should not enter the schema") &&
        Expect(describesProperties, "a schema should carry every declared property in order") &&
        Expect(carriesEnumNames, "an enum property should carry its name table") &&
        Expect(carriesAssetType, "an asset reference property should carry its asset type") &&
        Expect(carriesDefaults, "a schema property should carry the default instance's value") &&
        Expect(
            defaultsMatchInstance,
            "a component built from schema defaults should serialize like a default instance") &&
        Expect(rejectsGarbage, "an unreadable schema should be reported, not half-read") &&
        Expect(rejectsOtherVersion, "a schema from another version should be rejected") &&
        Expect(valuesRoundTrip, "property values should survive the trip through the file form") &&
        Expect(keepsFallback, "a value of the wrong shape should leave the fallback in place");
}

bool RunGameComponentEditingTests()
{
    const TestSupport::RegistryScope registries;
    using namespace GameEngine::Runtime;
    using GameEngine::Core::Json;
    namespace Serialization = GameEngine::Serialization;

    // 에디터가 게임 컴포넌트를 다루는 경로를 창 없이 검사한다. 위젯은 GameEditor에 있고,
    // 스키마·보존 컴포넌트·장면 왕복은 엔진에 있어 이 테스트에서 확인할 수 있다.
    //
    // 프로젝트 컴포넌트의 서술이 없는 프로세스를 재현하기 위해 진짜 타입에서 스키마를 뽑되
    // 그 팩토리는 등록하지 않는다. 스키마를 읽은 편집과 장면 저장·재로드를 함께 확인한다.
    const ComponentType* const gameTypes[] = { &SpriteAnimator::StaticType() };
    const std::vector<Serialization::ComponentSchema> schemas =
        Serialization::ParseComponentSchemas(Serialization::WriteComponentSchemas(gameTypes));
    const bool hasSchema = schemas.size() == 1 && schemas[0].typeName == "SpriteAnimator";
    if (!hasSchema)
    {
        return Expect(false, "the schema for the game component should have been written");
    }
    const Serialization::ComponentSchema& schema = schemas[0];


    ObjectRegistry registry;
    const Input input;
    RuntimeContext runtimeContext(registry, input);
    Scene scene(runtimeContext, "GameComponents");
    GameObject* const gameObject = scene.CreateGameObject("Actor");

    // Add Component: 스키마의 기본값을 실은 컴포넌트가 장면 로드와 같은 길로 붙는다. 이
    // 프로세스는 그 타입을 만들 줄 모르므로 보존 컴포넌트가 된다 — 아래 경고 로그는 의도된
    // 것이다.
    Component* const added = Serialization::SceneSerializer::LoadComponentIntoGameObject(
        Serialization::MakeDefaultComponentJson(schema), *gameObject);
    auto* const preserved = dynamic_cast<Serialization::PreservedComponent*>(added);
    const bool addsAsPreserved = preserved != nullptr &&
        preserved->GetPreservedTypeName() == "SpriteAnimator" &&
        preserved->GetData().Value("frameRate", 0.0f) > 0.0f;

    // 인스펙터의 행 하나를 편집하는 것과 같은 일: 스키마가 말한 종류로 값을 파일 표현으로
    // 바꾸고, 보존 JSON의 그 멤버만 갈아 끼운다.
    bool editsMember = false;
    if (preserved)
    {
        const Serialization::ComponentSchemaProperty* frameCount = nullptr;
        for (const Serialization::ComponentSchemaProperty& property : schema.properties)
        {
            if (property.name == "frameCount")
            {
                frameCount = &property;
            }
        }
        if (frameCount)
        {
            Json::Object members = preserved->GetData().AsObject();
            members.insert_or_assign(
                frameCount->name,
                Serialization::PropertyValueToJson(
                    frameCount->kind, PropertyValue{ 12 }, frameCount->enumNames));
            preserved->SetData(Json(std::move(members)));
            editsMember = preserved->GetData().Value("frameCount", 0) == 12;
        }
    }

    // 그리고 그 편집이 파일까지 간 뒤 되읽힌다. 사람이 값을 넣고 저장하고 다시 여는 것이
    // 이것이고, 그 사이에 값이 빠지면 편집은 없던 일이 된다.
    const std::string savedText = Serialization::SceneSerializer::SaveToText(scene);
    const std::span<const std::byte> savedBytes = std::as_bytes(std::span(savedText));
    const std::unique_ptr<Scene> reloaded = Serialization::SceneSerializer::LoadFromBytes(
        savedBytes, "GameComponents.scene", runtimeContext);
    const GameObject* const reloadedObject = reloaded ? reloaded->FindGameObject("Actor") : nullptr;
    const Serialization::PreservedComponent* reloadedComponent = nullptr;
    if (reloadedObject)
    {
        for (const std::unique_ptr<Component>& candidate : reloadedObject->GetAllComponents())
        {
            if (auto* const found =
                    dynamic_cast<Serialization::PreservedComponent*>(candidate.get()))
            {
                reloadedComponent = found;
            }
        }
    }
    const bool survivesTheFile = reloadedComponent != nullptr &&
        reloadedComponent->GetPreservedTypeName() == "SpriteAnimator" &&
        reloadedComponent->GetData().Value("frameCount", 0) == 12 &&
        reloadedComponent->GetData().Value("frameRate", 0.0f) ==
            preserved->GetData().Value("frameRate", 0.0f);

    // 원래대로 등록해 둔다: 표는 프로세스 전역이고 다음 스위트도 같은 프로세스에서 돈다.
    static_cast<void>(Serialization::RegisterComponentType(SpriteAnimator::StaticType()));

    return Expect(
            addsAsPreserved,
            "adding a game component should attach its schema defaults as preserved data") &&
        Expect(editsMember, "editing a schema row should rewrite that member of the JSON") &&
        Expect(
            survivesTheFile,
            "an edited game component should come back from the scene file unchanged");
}

static const TestSupport::Registration gObjectRegistryLifetimeTests{
    "RuntimeObject", "object registry lifetime tests should pass", RunObjectRegistryLifetimeTests };

static const TestSupport::Registration gComponentTypeTests{
    "RuntimeObject", "component type tests should pass", RunComponentTypeTests };

static const TestSupport::Registration gPropertyDescriptorTests{
    "RuntimeObject", "property descriptor tests should pass", RunPropertyDescriptorTests };

static const TestSupport::Registration gComponentPropertyDeclarationTests{
    "RuntimeObject", "component property declaration tests should pass", RunComponentPropertyDeclarationTests };

static const TestSupport::Registration gComponentCloneTests{
    "RuntimeObject", "component clone tests should pass", RunComponentCloneTests };

static const TestSupport::Registration gTransformWorldMatrixTests{
    "RuntimeObject", "transform world matrix tests should pass", RunTransformWorldMatrixTests };

static const TestSupport::Registration gSpriteAnimationTests{
    "RuntimeObject", "sprite animation tests should pass", RunSpriteAnimationTests };

static const TestSupport::Registration gTilemapTests{
    "RuntimeObject", "tilemap tests should pass", RunTilemapTests };

static const TestSupport::Registration gComponentStateContractTests{
    "RuntimeObject", "component state contract tests should pass", RunComponentStateContractTests };

static const TestSupport::Registration gComponentSchemaTests{
    "RuntimeObject", "component schema tests should pass", RunComponentSchemaTests };

static const TestSupport::Registration gGameComponentEditingTests{
    "RuntimeObject", "game component editing tests should pass", RunGameComponentEditingTests };

static const TestSupport::Registration gInputTests{
    "RuntimeObject", "input tests should pass", RunInputTests };
