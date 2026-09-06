#pragma once

#include "Component.h"

namespace GameEngine::Runtime
{

/// <summary>활성화 상태를 가지며 프레임 업데이트에 참여할 수 있는 컴포넌트이다.</summary>
class Behaviour : public Component
{
public:
    ~Behaviour() override = default;
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    [[nodiscard]] bool IsEnabled() const { return mEnabled; }
    void SetEnabled(bool enabled) { mEnabled = enabled; }

    /// <summary>Behaviour와 소유 GameObject 계층이 모두 활성 상태인지 확인한다.</summary>
    [[nodiscard]] bool IsActiveAndEnabled() const;

protected:
    /// <summary>활성 상태인 프레임마다 실행할 동작을 구현한다.</summary>
    virtual void UpdateBehaviour(float /*deltaTime*/) {}

private:
    void UpdateComponent(float deltaTime) final;

    bool mEnabled = true;
};

}
