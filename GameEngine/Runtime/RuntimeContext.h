#pragma once

namespace GameEngine::Runtime
{

class Input;
class ObjectRegistry;

/// <summary>
/// 장면의 객체들이 닿아도 되는 엔진 전역의 것들이다.
///
/// 서비스마다 `SceneManager`와 `Scene`의 생성자에 참조를 하나씩 꿰어 넘기지 않는다. 서비스가
/// 늘 때마다 그 사슬 전체가 넓어지기 때문이다.
///
/// 대신 레지스트리가 나머지 전부와 함께 이 안에 실려 다닌다. 컴포넌트가 닿을 수 있는 것은
/// 정확히 이 클래스이고, 그래서 목록이 검토 가능해진다: 여기에 추가하는 것은 게임플레이 코드가
/// 무엇에 의존해도 되는지에 대한 결정이며, 생성자를 넓히는 것이 아니라 한 곳에서 내려진다.
///
/// 의도적으로 여기 없는 것: 에셋 데이터베이스와 씬 매니저. 그것들은 컴포넌트가 프로젝트를
/// 로드하거나 어느 장면이 도는지 바꾸게 해 주는데, 업데이트 중에 그러는 컴포넌트는 지금 순회
/// 중인 것을 수정하는 셈이다. 둘은 프레임을 소유한 코드가 사는 `Game`을 통해 닿을 수 있는
/// 채로 남는다.
///
/// 아무것도 소유하지 않는다. `Game`이 자기가 소유한 서비스 위에 이것을 만들고 모든 객체보다
/// 오래 산다.
/// </summary>
class RuntimeContext final
{
public:
    RuntimeContext(ObjectRegistry& objectRegistry, const Input& input)
        : mObjectRegistry(&objectRegistry)
        , mInput(&input)
    {
    }

    /// <summary>이번 프레임에 키보드와 마우스가 한 일이다. 읽기 전용이다: 컴포넌트는 프레임의
    /// 나머지가 볼 것을 정하는 게 아니라 입력을 관찰한다.</summary>
    [[nodiscard]] const Input& GetInput() const { return *mInput; }

private:
    /// <summary>
    /// 객체가 인스턴스 id를 받아 오는 레지스트리다. 이 목록에서 유일하게 공개되지 않는 항목이며,
    /// 그 이유는 레지스트리가 여는 연산이 등록과 해제 — 객체 수명 관리 — 이기 때문이다. 그것은
    /// 객체를 만들고 거두는 코드의 일이지 컴포넌트가 볼 것이 아니다. 컴포넌트가 남을 등록하거나
    /// 자기를 해제할 수 있어야 할 이유는 없고, 할 수 있다면 그것은 "장면이 자기 객체를
    /// 소유한다"는 규칙이 우회되는 길이다.
    ///
    /// 그럼에도 레지스트리가 이 안에 실려 다니는 것은 <see cref="GameObject"/>가 만들어질 때
    /// 필요하기 때문이다 — 그래서 GameObject만 friend다. 컴포넌트는 자기 GameObject가 이미 속한
    /// 레지스트리에 소유자가 등록해 주므로 이 길이 애초에 필요 없다.
    /// </summary>
    friend class GameObject;
    [[nodiscard]] ObjectRegistry& GetObjectRegistry() const { return *mObjectRegistry; }

    ObjectRegistry* mObjectRegistry;
    const Input* mInput;
};

}
