#pragma once

#include <memory>
#include <span>
#include <vector>
#include <string_view>

namespace GameEngine::Runtime
{

class Component;
class PropertyDescriptor;

/// <summary>
/// 컴포넌트 클래스 하나의 정체성과, 그것이 상속하는 클래스이다.
///
/// 컴포넌트 클래스가 여기서 타입을 한 번 선언하고, "이게 뭐지"를 물어야 하는 모든 것 — 컴포넌트
/// 쿼리, 에디터의 이름 표시, 직렬화의 타입 문자열 — 이 같은 선언을 읽는다. 그래서 조회는
/// `dynamic_cast` 없이 답해지고, 타입 이름 표가 한 곳에만 있다.
///
/// 같은 이유로 속성 서술과 생성 훅도 이 선언에 붙는다: 컴포넌트가 무엇을 갖고 있고 어떻게
/// 만들어지는지를 직렬화·인스펙터·Add Component 목록이 저마다의 표가 아니라 클래스 자신의
/// 선언 한 곳에서 읽는다.
///
/// 정체성은 그 클래스 자신의 정적 인스턴스 주소라서, 비교는 포인터 비교이고 두 클래스가 충돌할
/// 수 없다. 이름은 진단과 도구를 위한 것이다.
/// </summary>
class ComponentType final
{
public:
    /// <summary>이 클래스가 선언한 속성들을 돌려주는 함수다. 함수-로컬 static 표를 가리킨다.</summary>
    using PropertyProvider = std::span<const PropertyDescriptor> (*)();

    /// <summary>이 클래스의 새 인스턴스를 만드는 함수다. `MakeComponentInstance<T>`가 그것이다.</summary>
    using InstanceFactory = std::unique_ptr<Component> (*)();

    /// <summary>
    /// 이 클래스가 함께 있어야 하는 컴포넌트들을 돌려주는 함수다. 속성 표와 같은 모양으로
    /// 함수-로컬 static 표를 가리킨다. 함수인 이유는 지연 때문이다: 선언되는 순간에 다른
    /// 타입의 <c>StaticType()</c>을 부르면 정적 초기화 순서에 기대게 된다.
    /// </summary>
    using RequirementProvider = std::span<const ComponentType* const> (*)();

    /// <summary>
    /// 컴포넌트 클래스를 선언한다. 사슬의 뿌리는 null 기반을 넘긴다. 속성이 없는 클래스와 만들 수
    /// 없는 클래스 — 추상 클래스, 객체의 일부라 따로 만들지 않는 Transform — 는 훅을 비워 둔다.
    /// </summary>
    constexpr ComponentType(
        const std::string_view name,
        const ComponentType* const base,
        const PropertyProvider properties = nullptr,
        const InstanceFactory createInstance = nullptr,
        const RequirementProvider requirements = nullptr)
        : mName(name),
          mBase(base),
          mProperties(properties),
          mCreateInstance(createInstance),
          mRequirements(requirements)
    {
    }

    ComponentType(const ComponentType&) = delete;
    ComponentType& operator=(const ComponentType&) = delete;
    ComponentType(ComponentType&&) = delete;
    ComponentType& operator=(ComponentType&&) = delete;

    [[nodiscard]] constexpr std::string_view GetName() const { return mName; }

    /// <summary>이 클래스가 상속하는 클래스이다. 사슬의 뿌리라면 null이다.</summary>
    [[nodiscard]] constexpr const ComponentType* GetBase() const { return mBase; }

    /// <summary>
    /// 이것이 `other`이거나 그것을 상속하는지 여부이다. 사슬을 걷는 것이 Renderer 같은 기반
    /// 클래스 쿼리가 MeshRenderer를 여전히 찾게 하는 방법이고, 동등 비교라면 찾지 못했을 것이다.
    /// 사슬은 몇 단 깊이이고 각 단은 포인터 비교다.
    /// </summary>
    [[nodiscard]] constexpr bool IsDerivedFrom(const ComponentType& other) const
    {
        for (const ComponentType* type = this; type != nullptr; type = type->mBase)
        {
            if (type == &other)
            {
                return true;
            }
        }
        return false;
    }

    /// <summary>
    /// 이 클래스 자신이 선언한 속성들이다. 기반의 것은 포함하지 않는다 — 사슬 전체는
    /// `CollectProperties`가 답한다. 선언하지 않은 클래스는 빈 목록이다.
    /// </summary>
    [[nodiscard]] std::span<const PropertyDescriptor> GetOwnProperties() const;

    /// <summary>이 타입을 이름으로 — Add Component 목록, 장면 로더 — 만들 수 있는지다.</summary>
    [[nodiscard]] constexpr bool IsCreatable() const { return mCreateInstance != nullptr; }

    /// <summary>이 타입의 새 인스턴스다. 생성 훅이 없는 타입이면 null이다.</summary>
    [[nodiscard]] std::unique_ptr<Component> CreateInstance() const;

    /// <summary>
    /// 이 클래스가 함께 있어야 하는 컴포넌트들이다. 기반의 것은 포함하지 않는다 — 사슬
    /// 전체는 <c>CollectRequiredComponents</c>가 답한다. 선언하지 않은 클래스는 빈 목록이다.
    /// </summary>
    [[nodiscard]] std::span<const ComponentType* const> GetOwnRequiredComponents() const;

    /// <summary>
    /// 이 클래스와 그 기반들이 함께 있어야 한다고 선언한 컴포넌트 전부다. 같은 것이 두 번
    /// 나오지는 않는다.
    /// </summary>
    [[nodiscard]] std::vector<const ComponentType*> CollectRequiredComponents() const;

private:
    std::string_view mName;
    const ComponentType* mBase;
    PropertyProvider mProperties;
    InstanceFactory mCreateInstance;
    RequirementProvider mRequirements;
};

/// <summary>
/// `StaticType` 선언에 넘기는 생성 훅이다. 기본 생성만 하고, 레지스트리 등록은
/// 소유자가 — `GameObject::AddComponent`가 — 한다.
/// </summary>
template <typename TComponent>
[[nodiscard]] std::unique_ptr<Component> MakeComponentInstance()
{
    return std::make_unique<TComponent>();
}

}
