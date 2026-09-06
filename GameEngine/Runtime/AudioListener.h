#pragma once

#include "Behaviour.h"

namespace GameEngine::Runtime
{

/// <summary>
/// 장면에서 소리를 듣는 지점을 표시하는 마커 컴포넌트이다. 저장하는 상태가 없다: 위치는
/// `AudioSystem`이 동기화마다 `GetGameObject()->GetTransform().GetWorldPosition()`으로 직접
/// 읽는다 — Camera가 자기 위치를 캐시하지 않는 것과 같은 이유로, Transform이 진실의 유일한
/// 자리로 남는다.
/// 활성 리스너가 없는 동안 오디오 출력은 무음이지만 클립의 재생 시간은 계속 진행한다.
/// </summary>
class AudioListener final : public Behaviour
{
public:
    // 여러 활성 리스너가 있으면 AudioSystem이 인스턴스 ID가 가장 작은 하나의 위치를 사용한다.
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }
};

}
