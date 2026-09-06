#pragma once

#include "Behaviour.h"

namespace GameEngine::Runtime
{

/// <summary>사용자 게임 로직을 위한 Unity 스타일 수명주기 컴포넌트 기반 클래스이다.</summary>
class MonoBehaviour : public Behaviour
{
public:
    ~MonoBehaviour() override = default;
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

protected:
    virtual void Awake() {}
    /// <summary>이 Behaviour가 활성화된 상태로 처음 업데이트되기 직전에 한 번 호출된다.</summary>
    /// <remarks>처음에 비활성화되어 있으면 나중에 처음 활성화될 때까지 호출되지 않는다.</remarks>
    virtual void Start() {}
    virtual void Update(float /*deltaTime*/) {}
    virtual void OnDestroy() {}

private:
    void OnAttached() final;
    void OnRemoved() final;
    void UpdateBehaviour(float deltaTime) final;

    bool mStarted = false;
};

}
