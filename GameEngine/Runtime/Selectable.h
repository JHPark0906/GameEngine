#pragma once

#include "Behaviour.h"

namespace GameEngine::Runtime
{

class UIEventSystem;

/// <summary>
/// 커서를 받는 UI 요소의 공통 기반이다. Unity의 <c>Selectable</c>에서 이름을 빌렸으나
/// <b>색과 틴트는 갖지 않는다</b> — 여기서 공통인 것은 겉모습이 아니라 「누가 커서를 받는가」와
/// 「받지 않게 됐을 때 무엇을 놓는가」다. 상태에 따라 색을 바꾸는 것은 지금 Button 하나뿐이고,
/// 틴트를 이 기반으로 올리면 드롭다운과 입력 필드에 필요 없는 상태가 생기므로 Button에 둔다.
///
/// 이 기반이 답하는 물음은 셋이다: 이 요소가 지금 입력을 받는지, 어떤 점을 덮는지, 키보드
/// 포커스를 받을 수 있는지. 셋 다 <see cref="UIEventSystem"/>이 후보를 모을 때 묻는 것이고,
/// 부류마다 따로 물으면 부류가 늘 때마다 이벤트 시스템에 같은 모양의 가지가 하나씩 는다.
///
/// 반대로 <b>프레임마다의 배달은 여기 없다</b>. 버튼이 받는 것은 hover·press·click이고,
/// 드롭다운은 커서 좌표와 다른 곳의 눌림과 키를, 입력 필드는 포커스와 편집 입력과 클립보드
/// 왕복을 받는다. 셋을 한 함수로 묶으면 아무도 쓰지 않는 인자를 모두가 들고 다니게 되므로,
/// 배달은 부류별로 남는다.
///
/// 만들 수 있는 타입이 아니다 — 생성 훅이 없으므로 Add Component 목록과 장면 로더가 이 이름을
/// 받지 않는다. 그래도 타입을 선언하는 이유는 <c>IsDerivedFrom</c>이 이 주소를 필요로 하기
/// 때문이다: 그것이 있어야 <c>GetComponents&lt;Selectable&gt;()</c>가 버튼과 드롭다운과 필드를
/// 한 번에 찾는다.
/// </summary>
class Selectable : public Behaviour
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>
    /// 이 요소가 입력을 받는지다. 거짓이면 커서를 가로채지 않으므로, 겹쳐 있는 아래 요소가
    /// 대신 받는다 — 받지 않는 요소가 클릭을 삼키고 아무 일도 하지 않는 것이 UI에서 가장 흔한
    /// "고장처럼 보이는 정상"이다.
    /// </summary>
    [[nodiscard]] bool IsInteractable() const { return mInteractable; }

    /// <summary>
    /// 받는지를 정한다. 받지 않게 되는 순간 그 요소가 쥐고 있던 일시 상태를 놓는다 — 무엇을
    /// 놓아야 하는지는 부류가 알므로 <see cref="OnInteractableLost"/>에게 묻는다.
    /// </summary>
    void SetInteractable(bool interactable);

    /// <summary>
    /// 이 요소가 그 점을 덮는지다. 기본은 자기 <see cref="RectTransform"/>의 <b>보이는</b>
    /// 사각형이며, 마스크에 가려진 절반은 덮지 않는다 — 그래서 잘림 규칙이 이벤트 시스템에
    /// 다시 적히지 않는다. 자기 사각형 밖까지 덮는 요소(펼쳐진 드롭다운)가 재정의한다.
    /// </summary>
    [[nodiscard]] virtual bool Covers(float x, float y) const;

    /// <summary>
    /// 이 요소가 키보드 포커스를 받을 수 있는지다. 기본은 거짓이며, 글자를 받는 것만 참이다.
    /// 누름이 포커스를 옮기므로, 이 답이 곧 "여기를 누르면 이전 필드의 타이핑이 끊기는가"다.
    /// </summary>
    [[nodiscard]] virtual bool TakesFocus() const { return false; }

protected:
    Selectable() = default;

    /// <summary>
    /// 이 요소가 입력을 받지 않게 된 순간이다. 쥐고 있던 일시 상태를 여기서 놓는다 — 버튼은
    /// 보이는 눌림을, 드롭다운은 펼침을, 입력 필드는 포커스를 놓는다. 놓지 않으면 아무도 접을
    /// 수 없는 목록이나, 보이지 않는 곳으로 계속 들어가는 타이핑이 남는다.
    /// </summary>
    virtual void OnInteractableLost() {}

private:
    bool mInteractable = true;
};

}
