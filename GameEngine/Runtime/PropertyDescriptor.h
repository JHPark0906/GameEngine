#pragma once

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "../Assets/Asset.h"
#include "../Assets/AssetReference.h"
#include "../Diagnostics/Debug.h"
#include "../Math/Color.h"
#include "../Math/Vector.h"
// Make 헬퍼의 접근자 람다가 Component의 완전한 타입을 비의존 문맥에서 쓴다. ComponentType.h는
// PropertyDescriptor를 전방 선언만 하므로 순환이 아니다.
#include "Component.h"

namespace GameEngine::Runtime
{

class ComponentType;

/// <summary>
/// 복제와 로드가 속성들을 하나의 상태로 복원하는 범위다. 개별 setter의 편집 제약을 서로의
/// 기본값에 적용하면 정상 min/max도 손실되므로, 결합 제약은 가장 바깥 범위가 끝날 때 검사한다.
/// 이 범위가 끝날 때까지 컴포넌트는 살아 있어야 한다.
/// </summary>
class PropertyRestoreScope final
{
public:
    explicit PropertyRestoreScope(Component& component);
    ~PropertyRestoreScope();
    PropertyRestoreScope(const PropertyRestoreScope&) = delete;
    PropertyRestoreScope& operator=(const PropertyRestoreScope&) = delete;

private:
    Component& mComponent;
    bool mWasRestoring;
};

/// <summary>
/// 속성이 담는 값의 종류다. 직렬화는 이것으로 JSON 표현을 고르고, 인스펙터는 위젯을 고른다.
/// Enum은 값으로는 int이고, 이름 표가 직렬화 문자열과 에디터 레이블을 함께 준다.
/// </summary>
enum class PropertyKind : unsigned char
{
    Bool,
    Int,
    Float,
    String,
    Vector2,
    Vector3,
    Color,
    AssetReference,
    Enum,
};

/// <summary>
/// 속성 하나가 오갈 수 있는 값이다. 컴포넌트가 노출하는 모든 속성 타입의 합집합이며, Enum은
/// int로 실린다.
/// </summary>
using PropertyValue = std::variant<
    bool, int, float, std::string,
    Math::Vector2, Math::Vector3, Math::Color, Assets::AssetReference>;

/// <summary>속성의 취급 방식이다. 서술 한 곳에 적으면 직렬화와 에디터가 함께 따른다.</summary>
enum class PropertyTraits : unsigned char
{
    None = 0,
    /// <summary>저장하지 않는다. 매 프레임 덮어써지는 런타임 소유 상태가 이것이다.</summary>
    NotSerialized = 1 << 0,
    /// <summary>무효한 값 — 빈 AssetReference — 은 파일에 쓰지 않는다.</summary>
    OmitWhenInvalid = 1 << 1,
};

[[nodiscard]] constexpr PropertyTraits operator|(const PropertyTraits left, const PropertyTraits right)
{
    return static_cast<PropertyTraits>(
        static_cast<unsigned char>(left) | static_cast<unsigned char>(right));
}

[[nodiscard]] constexpr bool HasTrait(const PropertyTraits traits, const PropertyTraits trait)
{
    return (static_cast<unsigned char>(traits) & static_cast<unsigned char>(trait)) != 0;
}

/// <summary>
/// 컴포넌트 속성 하나의 서술이다: 이름, 종류, 그리고 값을 오가는 접근자.
///
/// 컴포넌트 클래스가 자기 `ComponentType` 선언 곁에서 속성을 한 번 서술하면, 직렬화 쓰기와
/// 읽기, 인스펙터 행, Add Component 목록이 전부 같은 서술을 읽는다. 같은 사실이 네 곳에 타입
/// 사다리로 흩어져 있으면, 컴포넌트를 추가하며 한 곳을 빼먹는 실수를 막을 것이 없다.
///
/// `name`은 직렬화 키이자 정체성이라 장면 파일 형식의 일부다. 파일의 표현과 런타임 표현이
/// 다르면 — Camera의 "orthographic" bool이 ProjectionMode enum을 감싸듯 — 이름은 파일을 따르고
/// 접근자가 변환한다.
/// </summary>
class PropertyDescriptor final
{
public:
    using Getter = std::function<PropertyValue(const Component&)>;
    using Setter = std::function<bool(Component&, const PropertyValue&)>;

    /// <summary>
    /// 속성 곁에 항상 같은 값으로 놓이는 이름→문자열 쌍이다. 파일 형식의 자기 서술 표기 —
    /// Transform 회전의 rotationUnit/rotationOrder — 가 이것이다: 값이 아니라 표기 규약이라
    /// 컴포넌트에 저장되지 않지만, 쓰기는 속성 곁에 함께 쓰고 읽기는 같은 값인지 검증한다.
    /// </summary>
    using Constant = std::pair<std::string, std::string>;

    PropertyDescriptor(
        std::string name,
        std::string displayName,
        const PropertyKind kind,
        Getter get,
        Setter set,
        const PropertyTraits traits = PropertyTraits::None,
        std::vector<std::string> enumNames = {},
        std::vector<Constant> constants = {},
        std::optional<Assets::AssetType> assetType = std::nullopt)
        : mName(std::move(name)),
          mDisplayName(std::move(displayName)),
          mKind(kind),
          mGet(std::move(get)),
          mSet(std::move(set)),
          mTraits(traits),
          mEnumNames(std::move(enumNames)),
          mConstants(std::move(constants)),
          mAssetType(assetType)
    {
    }

    /// <summary>직렬화 키다. 장면 파일 형식의 일부이므로 바꾸는 것은 형식 변경이다.</summary>
    [[nodiscard]] std::string_view GetName() const { return mName; }

    /// <summary>인스펙터가 보이는 이름이다. 파일에는 쓰이지 않는다.</summary>
    [[nodiscard]] std::string_view GetDisplayName() const { return mDisplayName; }

    [[nodiscard]] PropertyKind GetKind() const { return mKind; }
    [[nodiscard]] PropertyTraits GetTraits() const { return mTraits; }

    /// <summary>Enum 속성의 값 이름들이다. 인덱스가 곧 값이고, 다른 종류면 비어 있다.</summary>
    [[nodiscard]] std::span<const std::string> GetEnumNames() const { return mEnumNames; }

    /// <summary>이 속성 곁에 놓이는 상수 주석들이다. 대부분의 속성은 비어 있다.</summary>
    [[nodiscard]] std::span<const Constant> GetConstants() const { return mConstants; }

    /// <summary>
    /// AssetReference 속성이 원하는 에셋 종류다. 인스펙터가 선택지를 이 종류로 거르고, 다른
    /// 종류의 속성이면 비어 있다.
    /// </summary>
    [[nodiscard]] std::optional<Assets::AssetType> GetAssetType() const { return mAssetType; }

    /// <summary>이 속성의 현재 값이다. 서술을 소유한 타입의 컴포넌트에만 물을 수 있다.</summary>
    [[nodiscard]] PropertyValue Get(const Component& component) const { return mGet(component); }

    /// <summary>
    /// 값을 실제 setter를 거쳐 적용한다. 값이 이 속성의 종류가 아니거나 — Enum이면 이름 표
    /// 밖이거나 — 컴포넌트가 서술을 소유한 타입이 아니면 아무것도 바꾸지 않고 false다. 진단은
    /// 호출자의 몫이다: 직렬화는 이것을 형식 오류로, 인스펙터는 입력 거부로 옮긴다.
    /// </summary>
    [[nodiscard]] bool TrySet(Component& component, const PropertyValue& value) const
    {
        return mSet(component, value);
    }

private:
    std::string mName;
    std::string mDisplayName;
    PropertyKind mKind;
    Getter mGet;
    Setter mSet;
    PropertyTraits mTraits;
    std::vector<std::string> mEnumNames;
    std::vector<Constant> mConstants;
    std::optional<Assets::AssetType> mAssetType;
};

/// <summary>
/// 타입 사슬 전체의 속성이다. 기반이 먼저, 각 단계 안에서는 선언 순서다 — 직렬화가 쓰는 순서이자
/// 인스펙터가 보이는 순서이며, Renderer의 공유 속성이 파생 렌더러들에 이렇게 나타난다.
/// </summary>
[[nodiscard]] std::vector<const PropertyDescriptor*> CollectProperties(const ComponentType& type);

/// <summary>이름으로 속성을 찾는다. 파생이 기반을 가리므로, 파생 쪽에서 먼저 찾는다.</summary>
[[nodiscard]] const PropertyDescriptor* FindProperty(const ComponentType& type, std::string_view name);

namespace Detail
{
    /// <summary>값 타입에서 속성 종류를 얻는다. 표에 없는 타입은 속성이 될 수 없다.</summary>
    template <typename TValue>
    [[nodiscard]] constexpr PropertyKind KindOfValue()
    {
        if constexpr (std::is_same_v<TValue, bool>) { return PropertyKind::Bool; }
        else if constexpr (std::is_same_v<TValue, int>) { return PropertyKind::Int; }
        else if constexpr (std::is_same_v<TValue, float>) { return PropertyKind::Float; }
        else if constexpr (std::is_same_v<TValue, std::string>) { return PropertyKind::String; }
        else if constexpr (std::is_same_v<TValue, Math::Vector2>) { return PropertyKind::Vector2; }
        else if constexpr (std::is_same_v<TValue, Math::Vector3>) { return PropertyKind::Vector3; }
        else if constexpr (std::is_same_v<TValue, Math::Color>) { return PropertyKind::Color; }
        else if constexpr (std::is_same_v<TValue, Assets::AssetReference>)
        {
            return PropertyKind::AssetReference;
        }
        else
        {
            static_assert(
                !std::is_same_v<TValue, TValue>,
                "This value type cannot be a component property. Add it to PropertyValue and "
                "KindOfValue deliberately, or map it through custom accessors.");
        }
    }

    /// <summary>
    /// 접근이 서술을 소유한 타입의 컴포넌트에서 왔는지 확인한다. 서술자는 언제나 컴포넌트 자신의
    /// 타입 사슬에서 얻으므로 이 검사는 쿼리의 static_cast와 같은 불변식을 지키는데, 그 불변식은
    /// 타입 시스템 밖에 있으므로 신뢰하는 대신 검사한다 — 명령 목록이 다른 백엔드의 리소스를
    /// 거절하는 것과 같은 이유다.
    /// </summary>
    template <typename TComponent>
    [[nodiscard]] bool IsPropertyAccessValid(const Component& component, const std::string& name)
    {
        if (component.GetComponentType().IsDerivedFrom(TComponent::StaticType()))
        {
            return true;
        }
        Diagnostics::Debug::LogError(
            "A property descriptor was used on a component of another type. property=", name,
            ", owner=", TComponent::StaticType().GetName(),
            ", component=", component.GetComponentType().GetName());
        return false;
    }
}

/// <summary>
/// 접근자 한 쌍으로 속성을 서술한다. 값 종류는 getter의 반환 타입에서 추론된다. 접근자는 멤버
/// 함수 포인터나 호출 가능한 것 무엇이든 되므로, 파일의 표현이 런타임 표현과 다른 속성은 람다가
/// 변환한다. <see cref="MakeProperty"/>와 <see cref="MakeAssetProperty"/>가 이것을 감싼다.
/// </summary>
template <typename TComponent, typename TGetter, typename TSetter>
[[nodiscard]] PropertyDescriptor MakeAccessorProperty(
    std::string name, std::string displayName, TGetter getter, TSetter setter,
    const PropertyTraits traits, std::vector<PropertyDescriptor::Constant> constants,
    const std::optional<Assets::AssetType> assetType)
{
    using Value = std::decay_t<std::invoke_result_t<TGetter, const TComponent&>>;
    constexpr PropertyKind kind = Detail::KindOfValue<Value>();

    const std::string propertyName = name;
    return PropertyDescriptor(
        std::move(name), std::move(displayName), kind,
        [getter, propertyName](const Component& component) -> PropertyValue
        {
            if (!Detail::IsPropertyAccessValid<TComponent>(component, propertyName))
            {
                return Value{};
            }
            return std::invoke(getter, static_cast<const TComponent&>(component));
        },
        [setter, propertyName](Component& component, const PropertyValue& value) -> bool
        {
            if (!Detail::IsPropertyAccessValid<TComponent>(component, propertyName))
            {
                return false;
            }
            const Value* const held = std::get_if<Value>(&value);
            if (!held)
            {
                return false;
            }
            std::invoke(setter, static_cast<TComponent&>(component), *held);
            return true;
        },
        traits, {}, std::move(constants), assetType);
}

/// <summary>
/// 속성 하나를 서술한다. 값 종류는 getter의 반환 타입에서 추론된다. 접근자는 멤버 함수 포인터나
/// 호출 가능한 것 무엇이든 되므로, 파일의 표현이 런타임 표현과 다른 속성은 람다가 변환한다.
/// </summary>
template <typename TComponent, typename TGetter, typename TSetter>
[[nodiscard]] PropertyDescriptor MakeProperty(
    std::string name, std::string displayName, TGetter getter, TSetter setter,
    const PropertyTraits traits = PropertyTraits::None,
    std::vector<PropertyDescriptor::Constant> constants = {})
{
    return MakeAccessorProperty<TComponent>(
        std::move(name), std::move(displayName), getter, setter, traits, std::move(constants),
        std::nullopt);
}

/// <summary>
/// 에셋 참조 속성을 서술한다. 어느 종류의 에셋을 원하는지를 함께 적는다 — 인스펙터가 그 종류의
/// 에셋만 선택지로 보이고, 게임 컴포넌트의 스키마에도 같은 종류가 실린다. getter는
/// <see cref="Assets::AssetReference"/>를 돌려주어야 한다.
/// </summary>
/// <param name="assetType">이 속성이 가리키는 에셋의 종류다.</param>
template <typename TComponent, typename TGetter, typename TSetter>
[[nodiscard]] PropertyDescriptor MakeAssetProperty(
    std::string name, std::string displayName, const Assets::AssetType assetType,
    TGetter getter, TSetter setter, const PropertyTraits traits = PropertyTraits::None)
{
    using Value = std::decay_t<std::invoke_result_t<TGetter, const TComponent&>>;
    static_assert(
        std::is_same_v<Value, Assets::AssetReference>,
        "MakeAssetProperty requires an AssetReference-returning getter.");
    return MakeAccessorProperty<TComponent>(
        std::move(name), std::move(displayName), getter, setter, traits, {}, assetType);
}

/// <summary>
/// enum 속성을 서술한다. 이름 표의 인덱스가 곧 enum 값이므로, 열거자는 0부터 빈틈없이 이어져야
/// 한다. 이름은 직렬화 문자열이자 에디터 버튼 레이블이고, 표 밖의 값은 적용 대신 거부된다.
/// </summary>
template <typename TComponent, typename TGetter, typename TSetter>
[[nodiscard]] PropertyDescriptor MakeEnumProperty(
    std::string name, std::string displayName,
    const std::initializer_list<std::string_view> valueNames,
    TGetter getter, TSetter setter,
    const PropertyTraits traits = PropertyTraits::None)
{
    using Enum = std::decay_t<std::invoke_result_t<TGetter, const TComponent&>>;
    static_assert(std::is_enum_v<Enum>, "MakeEnumProperty requires an enum-returning getter.");

    std::vector<std::string> names;
    names.reserve(valueNames.size());
    for (const std::string_view valueName : valueNames)
    {
        names.emplace_back(valueName);
    }

    const std::string propertyName = name;
    const int valueCount = static_cast<int>(names.size());
    return PropertyDescriptor(
        std::move(name), std::move(displayName), PropertyKind::Enum,
        [getter, propertyName](const Component& component) -> PropertyValue
        {
            if (!Detail::IsPropertyAccessValid<TComponent>(component, propertyName))
            {
                return 0;
            }
            return static_cast<int>(std::invoke(getter, static_cast<const TComponent&>(component)));
        },
        [setter, propertyName, valueCount](Component& component, const PropertyValue& value) -> bool
        {
            if (!Detail::IsPropertyAccessValid<TComponent>(component, propertyName))
            {
                return false;
            }
            const int* const held = std::get_if<int>(&value);
            if (!held || *held < 0 || *held >= valueCount)
            {
                return false;
            }
            std::invoke(setter, static_cast<TComponent&>(component), static_cast<Enum>(*held));
            return true;
        },
        traits, std::move(names));
}

}
