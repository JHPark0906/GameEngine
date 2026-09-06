#pragma once

#include <vector>

#include "../Assets/AssetReference.h"
#include "../Core/Json.h"
#include <memory>

#include "ComponentType.h"
#include "Object.h"

namespace GameEngine::Runtime
{

class GameObject;
class RuntimeContext;
class Scene;
class Transform;
class PropertyRestoreScope;

/// <summary>GameObject에 부착되어 수명주기 콜백을 받는 기능 단위의 기반 클래스이다.</summary>
class Component : public Object
{
public:
    ~Component() override = default;

    /// <summary>Component 클래스 자신의 정체성이다. 모든 컴포넌트 타입 사슬의 뿌리다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();

    /// <summary>
    /// 이 컴포넌트 자신의 클래스 정체성이다. 모든 컴포넌트 클래스가 자기 타입을 선언하므로,
    /// 쿼리는 자기가 쥔 것이 무엇인지 RTTI에 물을 필요가 없다.
    /// </summary>
    [[nodiscard]] virtual const ComponentType& GetComponentType() const = 0;

    /// <summary>이 컴포넌트를 소유한 GameObject를 반환한다.</summary>
    /// <returns>부착된 GameObject의 비소유 포인터이며, 부착 전이나 파괴 후에는 nullptr이다.</returns>
    [[nodiscard]] GameObject* GetGameObject() const { return mGameObject; }

    /// <summary>소유 게임 오브젝트의 Transform을 반환한다.</summary>
    [[nodiscard]] Transform* GetTransform() const;

    /// <summary>소유 게임 오브젝트가 속한 Scene을 반환한다.</summary>
    [[nodiscard]] Scene* GetScene() const;

    /// <summary>
    /// 이 컴포넌트가 닿을 수 있는 엔진 서비스들이다 — 오늘은 입력이고, 그 밖에 무엇이든
    /// `RuntimeContext`에 신중히 추가되는 것들이다. 객체에 부착되기 전에만 null이다.
    /// </summary>
    [[nodiscard]] RuntimeContext* GetRuntimeContext() const;

    /// <summary>
    /// 이 컴포넌트가 필요로 하는 에셋들이다. 프레임이 요구하기 전에 로드될 수 있게 한다.
    ///
    /// 컴포넌트는 자기가 어느 에셋을 쓰는지 말할 뿐, 그것이 어떻게 그려지는지는 말하지 않는다.
    /// 이것이 존재할 수 있는 이유가 그 차이다: draw는 렌더링 계층의 타입 없이는 기술할 수 없지만
    /// `AssetReference`는 런타임 타입이라서, 런타임이 렌더링에 의존하지 않고도 컴포넌트가 이
    /// 질문에 답할 수 있다.
    ///
    /// 아무것도 참조하지 않는 컴포넌트는 목록을 그대로 두며, 대부분이 그렇다.
    /// </summary>
    virtual void CollectAssetReferences(std::vector<Assets::AssetReference>&) const {}

    /// <summary>
    /// 속성으로 기술할 수 없는 상태를 컴포넌트 JSON에 함께 쓴다. 기본은 아무것도 쓰지 않는다.
    ///
    /// 이 짝이 "컴포넌트의 상태 = 선언된 속성 + 여기 쓰는 것"이라는 계약의 나머지 절반이다.
    /// 장면 저장·로드뿐 아니라 복제(<c>Clone</c>)와 에디터 undo 스냅숏도 같은 짝을 통과하므로,
    /// 여기에 쓴 상태는 그 세 경로 모두에서 함께 옮겨진다 — 경로마다 따로 손볼 곳은 없다.
    ///
    /// 거의 모든 컴포넌트의 상태는 속성 서술로 온전히 기술되고, 직렬화는 그 서술만 읽으면
    /// 된다. 타일맵의 타일 배열처럼 값 하나가 아니라 구조 — 배열이나 중첩 객체 — 인 상태만
    /// 이 자리를 쓴다. 여기에 담는 멤버 이름은 속성 이름과 겹치지 않아야 하며, 겹치면 속성이
    /// 이긴다.
    /// </summary>
    /// <param name="members">컴포넌트 JSON에 더할 멤버들을 받는다.</param>
    virtual void WriteExtraSerializedState(Core::Json::Object& members) const
    {
        static_cast<void>(members);
    }

    /// <summary>
    /// <see cref="WriteExtraSerializedState"/>가 쓴 것을 되읽는다. 기본은 아무것도 읽지 않는다.
    /// 없는 멤버는 기본값을 남긴다 — 속성 읽기와 같은 규칙이다.
    /// </summary>
    /// <param name="json">이 컴포넌트의 JSON 전체다.</param>
    virtual void ReadExtraSerializedState(const Core::Json& json) { static_cast<void>(json); }

protected:
    /// <summary>여러 속성을 함께 복원 중인지다. 서로를 제한하는 속성은 이 동안 독립적으로 받는다.</summary>
    [[nodiscard]] bool IsRestoringProperties() const { return mRestoringProperties; }

    /// <summary>모든 속성을 받은 뒤 결합 제약을 복구한다. 예외로 복원이 중단된 경우에도 호출된다.</summary>
    virtual void OnPropertiesRestored() noexcept {}

    /// <summary>GameObject에 부착된 직후 호출되는 엔진 수명주기 지점이다.</summary>
    virtual void OnAttached() {}

    /// <summary>GameObject에서 제거되기 직전에 호출되는 엔진 수명주기 지점이다.</summary>
    virtual void OnRemoved() {}

    /// <summary>프레임 업데이트가 필요한 컴포넌트가 구현하는 엔진 수명주기 지점이다.</summary>
    virtual void UpdateComponent(float /*deltaTime*/) {}

private:
    friend class GameObject;
    friend class PropertyRestoreScope;

    void AttachTo(GameObject& gameObject);
    void Tick(float deltaTime);
    void Destroy();

    /// <summary>
    /// 게임 오브젝트 복제에 사용할 새 컴포넌트를 만든다.
    ///
    /// 기본 구현은 타입의 생성 훅으로 새 인스턴스를 만들고, 컴포넌트 상태의 정의 그대로 —
    /// 선언된 속성 전체와 <see cref="WriteExtraSerializedState"/>가 쓰는 속성 밖 상태 — 를
    /// 옮긴다. 그래서 타일맵의 격자처럼 구조로 실린 상태도 복제를 위해 따로 쓸 코드 없이
    /// 따라온다. 속성으로도 그 왕복으로도 기술되지 않는 상태를 쥔 클래스 — 보존된 JSON을 쥔
    /// PreservedComponent — 만 override한다. 생성 훅이 없는 타입의 기본 구현은 null을
    /// 반환하고, 호출자가 경고를 남긴다 — Transform이 override를 유지하는 이유다.
    /// </summary>
    [[nodiscard]] virtual std::unique_ptr<Component> Clone() const;

    GameObject* mGameObject = nullptr;
    bool mDestroyed = false;
    bool mRestoringProperties = false;
};

}
