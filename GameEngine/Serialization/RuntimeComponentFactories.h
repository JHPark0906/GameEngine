#pragma once

#include <span>

namespace GameEngine::Runtime
{
class ComponentType;
}

namespace GameEngine::Serialization
{

/// <summary>
/// 장면 파일이 다루는 엔진 컴포넌트 타입들이다. 일반 등록과 도구의 엔진/게임 타입 구분이
/// 이 목록을 사용한다. 생성 가능한 전체 목록은 RegisteredComponentTypes와 현재 팩토리를 본다.
/// </summary>
[[nodiscard]] std::span<const Runtime::ComponentType* const> EngineComponentTypes();

/// <summary>
/// 컴포넌트 타입 하나를 장면 로더에 붙인다: 그 타입의 이름으로 일반 팩토리가 등록되어, 로드가
/// 타입의 속성 서술로 인스턴스를 만들고 채운다. 타입은 자기 속성과 생성 훅을 선언해야 하며,
/// 프로젝트가 정의한 컴포넌트가 손으로 쓴 JSON 파싱 없이 직렬화에 참여하는 길이 이 한 줄이다 —
/// 엔진 자신의 타입들도 같은 길로 등록된다.
/// </summary>
[[nodiscard]] bool RegisterComponentType(const Runtime::ComponentType& type);

/// <summary>
/// 이 프로세스가 <see cref="RegisterComponentType"/>으로 등록한 컴포넌트 타입 전체다. 엔진의
/// 타입과 게임 프로젝트가 등록한 타입이 등록 순서대로 함께 들어 있다.
///
/// 팩토리 표는 "이 이름을 만들 수 있는가"에만 답하므로, "이 프로세스가 아는 타입이 무엇인가"는
/// 이 목록이 답한다. 컴포넌트 스키마 산출이 그 질문이다: 게임 실행 파일이 자기가 아는 타입을
/// 적어 두면, 게임을 링크하지 않는 에디터가 그 파일로 게임 컴포넌트를 다룬다.
///
/// 등록이 취소된 타입은 빠지지 않는다 — 취소는 테스트가 자기 타입을 치우는 수단이고, 목록은
/// 프로세스가 한 번 알았던 것을 말한다.
/// </summary>
[[nodiscard]] std::span<const Runtime::ComponentType* const> RegisteredComponentTypes();

/// <summary>엔진 런타임 컴포넌트 타입들을 장면 로더에 등록한다.</summary>
[[nodiscard]] bool RegisterRuntimeComponentFactories();

}
