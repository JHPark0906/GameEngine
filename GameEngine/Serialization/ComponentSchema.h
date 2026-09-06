#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "../Core/Json.h"
#include "../Runtime/PropertyDescriptor.h"

namespace GameEngine::Runtime
{
class ComponentType;
}

namespace GameEngine::Serialization
{

/// <summary>
/// 컴포넌트 속성 하나를, 그 속성을 선언한 코드 없이도 다룰 수 있을 만큼 적은 것이다.
///
/// 서술자(<see cref="Runtime::PropertyDescriptor"/>)에서 접근자만 뺀 것과 같다: 접근자는 그
/// 타입의 코드이므로 프로세스 경계를 넘지 못하고, 나머지 — 이름, 종류, enum 이름 표, 상수,
/// 기본값 — 는 넘는다. 에디터는 이것으로 행을 그리고 값을 JSON에 되쓴다.
/// </summary>
struct ComponentSchemaProperty
{
    /// <summary>직렬화 키다. 장면 파일의 멤버 이름이 이것이다.</summary>
    std::string name;
    /// <summary>인스펙터가 보이는 이름이다.</summary>
    std::string displayName;
    Runtime::PropertyKind kind = Runtime::PropertyKind::Float;
    /// <summary>Enum 속성의 값 이름들이다. 인덱스가 곧 값이고, 다른 종류면 비어 있다.</summary>
    std::vector<std::string> enumNames;
    /// <summary>속성 곁에 늘 같은 값으로 놓이는 표기 규약들이다. 대부분 비어 있다.</summary>
    std::vector<Runtime::PropertyDescriptor::Constant> constants;
    /// <summary>AssetReference 속성이 원하는 에셋 종류다. 다른 종류의 속성이면 비어 있다.</summary>
    std::optional<Assets::AssetType> assetType;
    /// <summary>
    /// 기본 인스턴스가 가진 값을 장면 파일의 표현으로 적은 것이다. 새 컴포넌트를 만들 때
    /// 채워지는 값이며, 이것이 없으면 에디터는 "무엇을 쓸지"를 스스로 지어내야 한다.
    /// </summary>
    Core::Json defaultValue;
    /// <summary>저장하지 않는 속성이다. 에디터도 이런 속성은 그리지 않는다.</summary>
    bool notSerialized = false;
    /// <summary>무효한 에셋 참조는 파일에 쓰지 않는다.</summary>
    bool omitWhenInvalid = false;
};

/// <summary>컴포넌트 타입 하나의 스키마다: 타입 이름과 속성 표.</summary>
struct ComponentSchema
{
    /// <summary>장면 파일의 "type" 값이다.</summary>
    std::string typeName;
    std::vector<ComponentSchemaProperty> properties;
};

/// <summary>
/// 게임 실행 파일이 자기가 아는 컴포넌트 타입을 적어 두는 파일의 이름이다. 프로젝트 서술자
/// 옆에 놓이며, 에디터는 프로젝트를 열 때 그 자리에서 찾는다.
/// </summary>
inline constexpr const char* ComponentSchemaFileName = "Components.schema.json";

/// <summary>
/// 컴포넌트 타입들을 스키마 JSON 텍스트로 쓴다.
///
/// 에디터는 게임 프로젝트를 링크하지 않는다. 이 파일이 없으면 게임이 정의한 컴포넌트에 대해
/// 에디터가 아는 것은 장면 파일에 실려 온 JSON뿐이라 — 이름은 보이지만 어떤 속성이 있는지, 그
/// 속성이 무슨 종류인지, 새로 만들면 무엇이 들어가야 하는지는 알 수 없어 — 게임 컴포넌트를
/// 추가할 수도 편집할 수도 없다.
///
/// 이 파일이 그 앎을 프로세스 경계 너머로 옮긴다. 타입이 이미 자기 속성을 선언하고 있으므로,
/// 새로 적을 것은 없고 있는 선언을 내보내기만 하면 된다.
///
/// 기본값은 그 타입의 기본 인스턴스에서 읽는다. 생성 훅이 없는 타입 — 만들 수 없는 타입 — 은
/// 기본값 없이 속성만 적힌다.
/// </summary>
/// <param name="types">적을 컴포넌트 타입들이다.</param>
/// <returns>파일에 그대로 쓸 수 있는 JSON 텍스트다.</returns>
[[nodiscard]] std::string WriteComponentSchemas(
    std::span<const Runtime::ComponentType* const> types);

/// <summary>
/// <see cref="WriteComponentSchemas"/>가 쓴 텍스트를 되읽는다.
///
/// 형식이 아니거나 판이 다른 파일은 빈 목록이며, 무엇이 잘못됐는지는 로그가 말한다 — 스키마가
/// 없는 것과 읽지 못한 것은 사람에게 다른 사실이고, 조용히 빈 목록을 보이면 그 차이가 사라진다.
/// </summary>
/// <param name="text">스키마 파일의 내용이다.</param>
/// <returns>읽어 낸 타입들이며, 읽을 수 없으면 비어 있다.</returns>
[[nodiscard]] std::vector<ComponentSchema> ParseComponentSchemas(std::string_view text);

/// <summary>이 스키마의 기본값들로 채운 컴포넌트 JSON이다. "type"을 포함한다.</summary>
/// <param name="schema">만들 컴포넌트의 스키마다.</param>
/// <returns>장면 로더가 읽는 형식 그대로의 컴포넌트 JSON이다.</returns>
[[nodiscard]] Core::Json MakeDefaultComponentJson(const ComponentSchema& schema);

/// <summary>
/// 속성 값 하나를 장면 파일의 표현으로 바꾼다. 종류가 정하는 표현이며, Enum은 이름 표의
/// 문자열이다.
/// </summary>
/// <param name="kind">속성의 종류다.</param>
/// <param name="value">바꿀 값이다.</param>
/// <param name="enumNames">Enum 속성의 이름 표다. 다른 종류면 무시된다.</param>
[[nodiscard]] Core::Json PropertyValueToJson(
    Runtime::PropertyKind kind, const Runtime::PropertyValue& value,
    std::span<const std::string> enumNames);

/// <summary>
/// 장면 파일의 표현을 속성 값으로 되읽는다. 형태가 맞지 않으면 fallback을 그대로 돌려준다 —
/// 로더가 없는 멤버에 기본값을 남기는 것과 같은 규칙이다.
/// </summary>
/// <param name="kind">속성의 종류다.</param>
/// <param name="json">읽을 값이다.</param>
/// <param name="enumNames">Enum 속성의 이름 표다.</param>
/// <param name="fallback">읽지 못했을 때 돌려줄 값이다.</param>
[[nodiscard]] Runtime::PropertyValue PropertyValueFromJson(
    Runtime::PropertyKind kind, const Core::Json& json, std::span<const std::string> enumNames,
    const Runtime::PropertyValue& fallback);

}
