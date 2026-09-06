#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameEngine::Runtime
{
class GameObject;
}

namespace GameEngine::Core
{
class Json;
}

namespace GameEngine::Serialization
{

/// <summary>
/// 컴포넌트 하나가 담은 값이 형식을 벗어났다는 오류다. 이름표에 없는 enum 문자열이 그 경우다.
///
/// 일반 형식 오류와 구분되는 이유는 판정의 범위 때문이다. 이것은 그 컴포넌트에 대한 판정이라
/// 장면 로더가 잡아서 해당 컴포넌트만 보존 데이터로 강등하고 나머지 장면은 그대로 연다. 반면
/// 상수 주석 불일치 — 파일이 다른 회전 표기를 말하는 경우 — 같은 오류는 장면 전체의 뜻이
/// 달라지므로 그대로 올라가 장면 로드를 무산시킨다. 이 구분이 없으면 enum 오타 하나가 편집
/// 중이던 장면 전체를 잃게 만든다.
/// </summary>
class ComponentValueError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// <summary>
/// 타입 이름으로 컴포넌트를 만들어 붙이는 팩토리 표이다. 장면 역직렬화가 컴포넌트 JSON을 만나면
/// 여기 등록된 팩토리에 넘기며, 엔진과 프로젝트가 같은 표에 자기 타입을 등록한다.
/// </summary>
class ComponentFactory final
{
public:
    using Factory = std::function<bool(const Core::Json&, Runtime::GameObject&)>;

    [[nodiscard]] static bool Register(std::string type, Factory factory);

    /// <summary>
    /// 등록을 되돌린다. 표는 프로세스 전역이므로, 테스트가 등록한 타입을 다음 테스트에게
    /// 남기지 않는 것이 이것의 쓰임이다. 등록되어 있지 않던 타입이면 false다.
    /// </summary>
    [[nodiscard]] static bool Unregister(std::string_view type);

    /// <summary>
    /// 지금 이 표에 있는 것 전부다. 타입 이름과 그것을 만드는 팩토리다.
    /// </summary>
    using Registrations = std::vector<std::pair<std::string, Factory>>;

    /// <summary>
    /// 표를 통째로 복사해 둔다.
    ///
    /// <b>시험이 서로에게서 격리되기 위한 것이며, 제품 경로는 부르지 않는다.</b> 시험은 자기
    /// 컴포넌트 타입을 등록해 두고 끝에서 지우는데, 그 지우기를 한 번 잊으면 다음 시험이
    /// 「이미 아는 타입」을 만나 이유 없이 다르게 답한다. 들어올 때 찍어 두고 나갈 때
    /// 되돌리면 그 잊음이 불가능해진다.
    /// </summary>
    [[nodiscard]] static Registrations Snapshot();

    /// <summary>
    /// 표를 스냅숏 시점으로 되돌린다. 그 뒤에 더해진 타입은 사라지고, 그 사이에 지워진 타입은
    /// 돌아온다.
    /// </summary>
    static void Restore(const Registrations& registrations);

    /// <summary>
    /// 이 타입의 팩토리가 등록되어 있는지 여부이다. 로더가 "모르는 타입"과 "만들다 실패한
    /// 타입"을 구분하는 데 쓴다: 앞의 것은 보존하고, 뒤의 것은 보존하면 깨진 데이터를 굳히는
    /// 셈이라 보존하지 않는다.
    /// </summary>
    [[nodiscard]] static bool IsRegistered(std::string_view type);
    [[nodiscard]] static bool Create(
        std::string_view type,
        const Core::Json& json,
        Runtime::GameObject& gameObject);
};

}
